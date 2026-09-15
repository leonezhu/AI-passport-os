#include "apps/home/home_upload.h"

#include "apps/home/home_profile.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <esp_random.h>      // esp_fill_random():每次开启上传窗口换一个一次性 token
#include "wifi_manager.h"

#include <cJSON.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <climits>

static const char *TAG = "home_upload";

static httpd_handle_t s_server;
static char s_url[32];

// 2026-09-13 修复(B8):上传接口原先完全无鉴权,同一局域网任何人都能 POST
// /api/profile 或 /api/avatar 改写名片与人像。现在每次开启上传窗口生成一个
// 一次性 token,页面里通过 window.__T 带上,三个 /api/* 接口都校验 ?t=。
// 这不是强认证(设备没有账号体系),但足以挡住"随手扫一下端口就能改"的滥用。
static uint32_t s_token = 0;

// 从 req->uri 的 query 里取 t= 并比对。URL 形如 /api/profile?t=123456
static bool token_ok(httpd_req_t *req) {
    if (s_token == 0) return false;
    char query[96], token[16];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "t", token, sizeof(token)) != ESP_OK) return false;
    for (const char *p = token; *p; ++p) if (*p < '0' || *p > '9') return false;
    char *end = nullptr;
    errno = 0;
    const unsigned long value = std::strtoul(token, &end, 10);
    return token[0] && end && !*end && errno != ERANGE && value <= UINT32_MAX &&
           static_cast<uint32_t>(value) == s_token;
}

// ---------------------------------------------------------------------------
// 配置网页。人像照片在浏览器端完成:拖动取景 + 缩放 → 亮度阈值 → 1bit 打包
// (96×150,按行 MSB,共 1800 字节),设备端不做任何图片处理。
// ---------------------------------------------------------------------------
static const char *PAGE = R"html(<!DOCTYPE html><html lang='zh'><head>
<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>首页配置</title><style>
body{font-family:ui-monospace,monospace;background:#181b1c;color:#eee7d6;max-width:420px;margin:0 auto;padding:16px}
h2{font-size:18px;margin:8px 0}label{display:block;font-size:14px;color:#b3b2a5;margin:12px 0 4px}
input[type=text]{width:100%;box-sizing:border-box;padding:10px;border:1px solid #59605d;border-radius:0;background:#202526;color:#eee7d6;font-size:16px}
button{margin-top:12px;width:100%;padding:12px;border:0;border-radius:0;background:#ddb36c;color:#181b1c;font-size:16px;font-weight:600}
button.grey{background:#59605d;color:#eee7d6}
#st{margin-top:12px;font-size:14px;color:#b3b2a5;min-height:18px;text-align:center;word-break:break-all}
#vp{position:relative;width:192px;height:300px;margin:8px auto 0;overflow:hidden;background:#202526;touch-action:none}
#pv{position:absolute;left:0;top:0;pointer-events:none;user-select:none;-webkit-user-drag:none}
#zm{display:block;width:192px;margin:6px auto 0}
#binc{display:block;margin:6px auto 0;width:96px;height:150px;background:#181b1c;image-rendering:pixelated}
#cap{margin:4px 0 0;font-size:12px;color:#b3b2a5;text-align:center}

/* Travel instrument: unboxed editorial headings; inputs retain affordance. */
h1,h2,h3{letter-spacing:.04em;font-weight:600;line-height:1.25}
p{line-height:1.65}
</style></head><body>
<h2>AI 护照 · 首页配置</h2>
<label>姓名</label><input type='text' id='name' maxlength='9'>
<label>个性签名</label><input type='text' id='sig' maxlength='30'>
<label>城市（保存前请先点"定位城市"）</label><input type='text' id='city' maxlength='15' placeholder='如：上海'>
<button class='grey' id='geo'>定位城市</button>
<label>人像照片（选图后拖动取景 · 滑杆缩放）</label>
<input type='file' id='file' accept='image/*'>
<div id='vp'><img id='pv'></div>
<input type='range' id='zm' min='100' max='400' value='100'>
<canvas id='binc' width='96' height='150'></canvas>
<div id='cap'>下图为设备上的点阵效果(亮=绿点)</div>
<button id='av'>上传人像</button>
<button id='save'>保存资料</button>
<div id='st'></div>
<script>
const $=i=>document.getElementById(i);const st=t=>{$('st').textContent=t};
let lat='',lon='';
$('city').oninput=()=>{lat='';lon=''};
async function load(){try{const r=await fetch('/api/profile?t='+window.__T);const j=await r.json();
$('name').value=j.name||'';$('sig').value=j.signature||'';$('city').value=j.city||'';
lat=j.lat||'';lon=j.lon||'';
st(j.avatar?'设备上已有头像,可直接更换':'尚未设置头像')}catch(e){st('读取失败,请确认连的是设备 Wi-Fi')}}
$('geo').onclick=async()=>{const c=$('city').value.trim();if(!c){st('请输入城市');return}
st('定位中…');try{
// count=5 且优先同名结果:count=1 会把"珠海"定位到外省的同名乡镇
const r=await fetch('https://geocoding-api.open-meteo.com/v1/search?count=5&language=zh&format=json&name='+encodeURIComponent(c));
const j=await r.json();const rs=(j.results||[]);
if(!rs.length){st('未找到该城市');return}
const norm=s=>(s||'').replace(/市$/,'');
const exact=rs.filter(g=>norm(g.name)===norm(c));
const cn=exact.filter(g=>(g.country_code||'')==='CN');
const g=cn[0]||exact[0]||rs[0];
lat=String(g.latitude);lon=String(g.longitude);
st('已定位:'+(g.admin1?g.admin1+' ':'')+g.name+'('+lat+','+lon+')')}catch(e){st('定位失败,请检查手机网络')}}
// ---- 手动取景:选图 → 拖动/缩放 → 实时点阵预览 → 上传 ----
// 取景窗 192×300(设备像素的 2 倍),上传/预览时按 0.5 映射回 96×150。
const VP={w:192,h:300};
let zoom=1,ox=0,oy=0,S0=1,hasImg=false;
const S=()=>S0*zoom;
function clampView(){const w=natW()*S(),h=natH()*S();
 ox=Math.min(0,Math.max(VP.w-w,ox));oy=Math.min(0,Math.max(VP.h-h,oy));
 $('pv').style.width=w+'px';$('pv').style.left=ox+'px';$('pv').style.top=oy+'px'}
function natW(){return $('pv').naturalWidth}function natH(){return $('pv').naturalHeight}
function drawCrop(g){const s=S();
 g.drawImage($('pv'),-ox/s,-oy/s,VP.w/s,VP.h/s,0,0,96,150)}
function binPreview(){const c=$('binc'),g=c.getContext('2d');
 const tmp=document.createElement('canvas');tmp.width=96;tmp.height=150;
 drawCrop(tmp.getContext('2d'));
 const d=tmp.getContext('2d').getImageData(0,0,96,150).data,o=g.createImageData(96,150);
 for(let i=0;i<d.length;i+=4){const l=(d[i]*77+d[i+1]*151+d[i+2]*28)>>8;
  o.data[i]=140;o.data[i+1]=245;o.data[i+2]=166;o.data[i+3]=l>=110?255:36}
 g.putImageData(o,0,0)}
$('file').onchange=e=>{const f=e.target.files[0];if(!f)return;
const img=new Image();img.onload=()=>{$('pv').src=img.src;hasImg=true;
S0=Math.max(VP.w/natW(),VP.h/natH());zoom=1;$('zm').value=100;
ox=(VP.w-natW()*S())/2;oy=(VP.h-natH()*S())/2;clampView();binPreview()};
img.src=URL.createObjectURL(f)};
$('zm').oninput=()=>{if(!hasImg)return;
const nz=$('zm').value/100,cx=VP.w/2,cy=VP.h/2;   // 以取景窗中心为缩放锚点
ox=cx-(cx-ox)*nz/zoom;oy=cy-(cy-oy)*nz/zoom;zoom=nz;clampView();binPreview()};
let drag=null;
$('vp').onpointerdown=e=>{if(!hasImg)return;drag={x:e.clientX,y:e.clientY,ox:ox,oy:oy};
 $('vp').setPointerCapture(e.pointerId)};
$('vp').onpointermove=e=>{if(!drag)return;ox=drag.ox+e.clientX-drag.x;
 oy=drag.oy+e.clientY-drag.y;clampView()};
$('vp').onpointerup=$('vp').onpointercancel=()=>{if(drag){drag=null;binPreview()}};
$('av').onclick=async()=>{if(!hasImg){st('请先选择图片');return}
st('上传中…');try{const c=document.createElement('canvas');c.width=96;c.height=150;
drawCrop(c.getContext('2d'));
const d=c.getContext('2d').getImageData(0,0,96,150).data;
// 亮度阈值 → 1bit,按行打包(每字节 8 像素,MSB 在前),共 1440 字节
const b=new Uint8Array(96*150/8);
for(let i=0,j=0;i<d.length;i+=4,j++){const l=(d[i]*77+d[i+1]*151+d[i+2]*28)>>8;
if(l>=110)b[j>>3]|=(128>>(j&7))}
const r=await fetch('/api/avatar?t='+window.__T,{method:'POST',body:b});
st(r.ok?'人像已更新 ✓ 设备上按上键刷新查看':'上传失败('+r.status+')')}catch(e){st('上传失败,请重试')}}
$('save').onclick=async()=>{const c=$('city').value.trim();
if(c&&!lat){st('城市尚未定位,请先点"定位城市"');return}
st('保存中…');try{
const r=await fetch('/api/profile?t='+window.__T,{method:'POST',headers:{'Content-Type':'application/json'},
body:JSON.stringify({name:$('name').value,signature:$('sig').value,city:c,lat:lat,lon:lon})});
if(r.ok){st('已保存 ✓')}else{st('保存失败('+r.status+',内容可能超长)')}}catch(e){st('保存失败')}}
load();
</script></body></html>)html";

// ---------------------------------------------------------------------------
// 处理器
// ---------------------------------------------------------------------------

static esp_err_t send_json(httpd_req_t *req, int status, const char *json) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, status == 200 ? "200 OK" :
                              status == 401 ? "401 Unauthorized" : "400 Bad Request");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t handler_index(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    // 先把一次性 token 注入成 window.__T,再原样输出页面 —— 用分块发送避免
    // 为了替换占位符而整页复制一遍(页面约 5KB,httpd 任务栈不宽裕)。
    char head[48];
    int n = std::snprintf(head, sizeof(head), "<script>window.__T='%lu'</script>",
                          (unsigned long)s_token);
    httpd_resp_sendstr_chunk(req, head);
    httpd_resp_sendstr_chunk(req, PAGE);
    httpd_resp_send_chunk(req, nullptr, 0);
    (void)n;
    return ESP_OK;
}

static esp_err_t handler_profile_get(httpd_req_t *req) {
    if (!token_ok(req)) return send_json(req, 401, "{\"error\":\"unauthorized\"}");
    home_profile_t profile;
    home_profile_get(&profile);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", profile.name);
    cJSON_AddStringToObject(root, "signature", profile.signature);
    cJSON_AddStringToObject(root, "city", profile.city);
    cJSON_AddStringToObject(root, "lat", profile.lat);
    cJSON_AddStringToObject(root, "lon", profile.lon);
    cJSON_AddBoolToObject(root, "avatar", home_profile_has_avatar());
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return send_json(req, 400, "{\"error\":\"oom\"}");
    esp_err_t err = send_json(req, 200, json);
    cJSON_free(json);
    return err;
}

static esp_err_t handler_profile_post(httpd_req_t *req) {
    if (!token_ok(req)) return send_json(req, 401, "{\"error\":\"unauthorized\"}");
    char body[384];
    const size_t len = (size_t)req->content_len;
    if (len == 0 || len > sizeof(body) - 1) {
        return send_json(req, 400, "{\"error\":\"bad size\"}");
    }
    int received = 0;
    while (received < (int)len) {
        int n = httpd_req_recv(req, body + received, len - received);
        if (n <= 0) return send_json(req, 400, "{\"error\":\"recv\"}");
        received += n;
    }
    body[received] = '\0';

    cJSON *root = cJSON_Parse(body);
    if (!root) return send_json(req, 400, "{\"error\":\"json\"}");

    // 只接受网页会填的字段;缺省按空串处理,等于允许清空资料
    home_profile_t in = {};
    const auto grab = [&](const char *key, char *dst, size_t cap) {
        const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
        const char *value = cJSON_IsString(item) ? item->valuestring : "";
        if (std::strlen(value) >= cap) return false;
        std::memcpy(dst, value, std::strlen(value) + 1);
        return true;
    };
    const bool valid = grab("name", in.name, HOME_NAME_MAX) &&
        grab("signature", in.signature, HOME_SIGNATURE_MAX) &&
        grab("city", in.city, HOME_CITY_MAX) &&
        grab("lat", in.lat, HOME_COORD_MAX) &&
        grab("lon", in.lon, HOME_COORD_MAX);
    cJSON_Delete(root);
    if (!valid) return send_json(req, 400, "{\"error\":\"text too long\"}");

    esp_err_t err = home_profile_set_text(&in);
    return send_json(req, err == ESP_OK ? 200 : 400,
                     err == ESP_OK ? "{\"ok\":true}" : "{\"error\":\"invalid\"}");
}

static esp_err_t handler_avatar_post(httpd_req_t *req) {
    if (!token_ok(req)) return send_json(req, 401, "{\"error\":\"unauthorized\"}");
    if ((size_t)req->content_len != HOME_AVATAR_BYTES) {
        ESP_LOGW(TAG, "Avatar upload wrong content-length %d", (int)req->content_len);
        return send_json(req, 400, "{\"error\":\"size\"}");
    }

    // httpd_req_recv 的缓冲是 char*,而落盘接口要 uint8_t*;在调用处做一次转换,
    // 比把整个缓冲声明成 uint8_t 再在 recv 上强转更安全(C++ 下 recv 不会接受 uint8_t*)。
    char chunk[512];
    size_t remaining = HOME_AVATAR_BYTES;
    bool first = true;
    while (remaining > 0) {
        int n = httpd_req_recv(req, chunk, remaining < sizeof(chunk) ? remaining : sizeof(chunk));
        if (n <= 0) {
            ESP_LOGW(TAG, "Avatar recv aborted at %u", (unsigned)(HOME_AVATAR_BYTES - remaining));
            return send_json(req, 400, "{\"error\":\"recv\"}");
        }
        // 2026-09-13 修复(B3):原来直接 remaining -= n。若对端发来超过剩余
        // 长度的字节(异常/分块编码/代理行为),size_t 下溢成巨大值 →
        // while 死循环,永久占住这个 httpd 会话与 6KB 栈任务。
        if ((size_t)n > remaining) {
            ESP_LOGW(TAG, "Avatar oversized: got %d, only %u left; truncating",
                     n, (unsigned)remaining);
            n = (int)remaining;
        }
        remaining -= (size_t)n;
        esp_err_t err = home_profile_write_avatar_chunk(reinterpret_cast<const uint8_t *>(chunk),
                                                        (size_t)n, first, remaining == 0);
        first = false;
        if (err != ESP_OK) return send_json(req, 400, "{\"error\":\"store\"}");
    }
    return send_json(req, 200, "{\"ok\":true}");
}

// ---------------------------------------------------------------------------

esp_err_t home_upload_start(void) {
    if (s_server) return ESP_OK;

    // 每个上传窗口换一个 token(见 token_ok 注释)。用 esp_fill_random 而非
    // esp_random():后者在同批 IDF 头文件组合下出现过未声明的编译问题,
    // 而 xiaozhi 侧已稳定使用 esp_fill_random。
    esp_fill_random(&s_token, sizeof(s_token));
    s_token &= 0x7fffffffu;
    if (s_token == 0) s_token = 1;   /* 0 被 token_ok 当作"未启用" */

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6144;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        s_server = NULL;
        return err;
    }

    const httpd_uri_t index_uri = {"/", HTTP_GET, handler_index, NULL};
    const httpd_uri_t profile_get = {"/api/profile", HTTP_GET, handler_profile_get, NULL};
    const httpd_uri_t profile_post = {"/api/profile", HTTP_POST, handler_profile_post, NULL};
    const httpd_uri_t avatar_post = {"/api/avatar", HTTP_POST, handler_avatar_post, NULL};
    // 2026-09-13 修复(B8):注册返回值必须检查。原来四个 handler 全丢返回值,
    // 注册失败时页面直接 404 且日志里一行痕迹都没有。
    struct { const httpd_uri_t *uri; const char *name; } routes[] = {
        {&index_uri, "/"}, {&profile_get, "GET /api/profile"},
        {&profile_post, "POST /api/profile"}, {&avatar_post, "POST /api/avatar"},
    };
    for (const auto &r : routes) {
        esp_err_t e = httpd_register_uri_handler(s_server, r.uri);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "register %s failed: %s", r.name, esp_err_to_name(e));
            httpd_stop(s_server);
            s_server = nullptr;
            s_token = 0;
            return e;
        }
    }

    snprintf(s_url, sizeof(s_url), "http://%s",
             WifiManager::GetInstance().GetIpAddress().c_str());
    ESP_LOGI(TAG, "Upload server started at %s", s_url);
    return ESP_OK;
}

void home_upload_stop(void) {
    if (!s_server) return;
    httpd_stop(s_server); // 内部 join 处理器任务后释放
    s_server = NULL;
    s_token = 0;          // 窗口关闭即作废,旧链接立刻失效
    ESP_LOGI(TAG, "Upload server stopped");
}

bool home_upload_active(void) { return s_server != NULL; }

const char *home_upload_url(void) { return s_server ? s_url : ""; }
