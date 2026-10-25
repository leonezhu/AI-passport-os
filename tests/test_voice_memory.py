"""Exercise production Voice storage with fragmented-heap and failure injection."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
text = (root / 'main/apps/voice/audio_streamer.c').read_text()
storage = text[text.index('typedef struct {'):text.index('#define s_ring_struct')]
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
typedef uint8_t StackType_t;
typedef struct { unsigned char data[128]; } StaticRingbuffer_t;
typedef struct { unsigned char data[344]; } StaticTask_t;
#define RING_BYTES 6432
#define CHUNK_ITEM_BYTES 3204
static int calls, fail_at, live;
static void *test_malloc(size_t size) {
    assert(size <= 8192); // Plenty of total RAM, but no session-sized hole.
    if (++calls == fail_at) return NULL;
    void *p = malloc(size); if (p) ++live; return p;
}
static void *test_calloc(size_t n, size_t size) {
    assert(n * size <= 8192);
    if (++calls == fail_at) return NULL;
    void *p = calloc(n,size); if (p) ++live; return p;
}
static void test_free(void *p) { if(p) --live; free(p); }
#define malloc test_malloc
#define calloc test_calloc
#define free test_free
''' + storage + r'''
int main(void) {
    for (int i=1;i<=5;i++) {
        fail_at=i; calls=0;
        assert(!stream_memory_alloc()); assert(!s_memory); assert(!live);
        stream_memory_free(); assert(!live);
    }
    fail_at=0;
    for(int i=0;i<100;i++) {
        assert(stream_memory_alloc()); assert(live==5);
        s_memory->audio_stack[3071]=1; s_memory->send_stack[4095]=2;
        s_memory->ring_storage[RING_BYTES-1]=3;
        s_memory->chunk[CHUNK_ITEM_BYTES-1]=4;
        stream_memory_free(); assert(!s_memory); assert(!live);
    }
}
'''
with tempfile.TemporaryDirectory() as temp:
    source = Path(temp)/'memory.c'
    binary = Path(temp)/'memory'
    source.write_text(code)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined',str(source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('Voice fragmented allocation, rollback and 100 release cycles: PASS')
