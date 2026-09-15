// Execute the firmware's actual embedded page script with a minimal DOM.
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const source = fs.readFileSync('main/apps/home/home_upload.cc', 'utf8');
const script = source.match(/<script>\n([\s\S]*?)<\/script><\/body>/)[1];
const elements = new Map();
const get = id => {
  if (!elements.has(id)) elements.set(id, {value:'',textContent:'',style:{}});
  return elements.get(id);
};
const requests = [];
const context = {document:{getElementById:get},window:{__T:314159},
  fetch:async (url, options) => {
    requests.push({url,options});
    return {ok:true,json:async()=>({name:'测试',city:'上海',lat:'31',lon:'121'})};
  }};
vm.runInNewContext(script, context);
(async()=>{
  // Finish initial fetch / JSON microtasks before editing the loaded form.
  await new Promise(resolve=>setImmediate(resolve));
  await get('save').onclick();
  const post=requests.find(r=>r.options?.method==='POST');
  assert.equal(post.url,'/api/profile?t=314159');
  assert.equal(JSON.parse(post.options.body).name,'测试');
  get('city').value='北京';
  get('city').oninput();
  const count=requests.length;
  await get('save').onclick();
  assert.equal(requests.length,count,'a changed city must be located before save');
  assert.match(get('st').textContent,/定位/);
  console.log('Profile page: PASS');
})().catch(e=>{console.error(e);process.exitCode=1});
