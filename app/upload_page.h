// ===========================================================================
// upload_page.h -- the web page served in upload mode
// ===========================================================================
// One self-contained page: no external scripts or fonts, because in hotspot
// mode the phone has no internet. It talks to the JSON endpoints in
// uploadmode.cpp. The server prepends "<script>var Z=true|false</script>" so
// the page follows the player's language setting.
#pragma once

#include <Arduino.h>

static const char UPLOAD_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>CYD MP3</title>
<style>
body{font-family:system-ui,-apple-system,sans-serif;background:#111;color:#eee;margin:0;padding:14px}
.w{max-width:640px;margin:0 auto}
h1{font-size:20px;margin:0 0 2px}
.sub{color:#888;font-size:13px;margin-bottom:12px}
.tabs{display:flex;gap:6px;margin-bottom:12px;flex-wrap:wrap}
.tabs button{flex:1;min-width:90px}
button{padding:9px 12px;border:0;border-radius:6px;background:#2a2a2a;color:#ddd;font-size:14px;cursor:pointer}
button.on,button.pri{background:#0aa;color:#fff;font-weight:600}
button.bad{background:#7a2020;color:#fff}
.box{border:1px solid #333;border-radius:8px;padding:12px;margin-bottom:12px}
.row{display:flex;align-items:center;gap:8px;padding:7px 2px;border-bottom:1px solid #222}
.row:last-child{border-bottom:0}
.nm{flex:1;word-break:break-all}
.sz{color:#888;font-size:12px;white-space:nowrap}
.ic{background:none;padding:4px 6px;font-size:16px}
.dir{color:#0dd;cursor:pointer}
.crumb{color:#0dd;margin-bottom:8px;font-size:14px}
.drop{border:2px dashed #444;border-radius:8px;padding:18px;text-align:center;color:#888;margin:10px 0}
.drop.hot{border-color:#0aa;color:#0dd}
.bar{height:6px;background:#333;border-radius:3px;overflow:hidden;margin-top:4px}
.bar i{display:block;height:100%;background:#0aa;width:0}
.q{font-size:13px;margin:6px 0}
.err{color:#f66}.ok{color:#6d6}
input{width:100%;box-sizing:border-box;padding:9px;border-radius:6px;border:1px solid #444;background:#1c1c1c;color:#eee;font-size:15px;margin:4px 0 10px}
img.th{width:56px;height:56px;object-fit:cover;border-radius:4px;background:#000}
.hint{color:#777;font-size:12px;line-height:1.5}
</style></head><body><div class="w">
<h1>CYD MP3</h1><div class="sub" id="info">…</div>
<div class="tabs">
 <button id="t_music" onclick="tab('music')"></button>
 <button id="t_bg" onclick="tab('bg')"></button>
 <button id="t_font" onclick="tab('font')"></button>
 <button id="t_wifi" onclick="tab('wifi')"></button>
</div>

<div id="p_music" class="box">
 <div class="crumb" id="crumb"></div>
 <div id="list"></div>
 <div class="drop" id="drop_music"></div>
 <input type="file" id="f_music" accept=".mp3,audio/mpeg" multiple style="display:none">
 <button onclick="$('f_music').click()" class="pri" id="b_pick"></button>
 <button onclick="mkdir()" id="b_mkdir"></button>
 <div id="q_music"></div>
</div>

<div id="p_bg" class="box" style="display:none">
 <div id="bglist"></div>
 <div class="drop" id="drop_bg"></div>
 <input type="file" id="f_bg" accept=".gif,image/gif" multiple style="display:none">
 <button onclick="$('f_bg').click()" class="pri" id="b_pickgif"></button>
 <div id="q_bg"></div>
 <p class="hint" id="h_bg"></p>
</div>

<div id="p_font" class="box" style="display:none">
 <div id="fontinfo" class="q"></div>
 <input type="file" id="f_font" accept=".vlw" style="display:none">
 <button onclick="$('f_font').click()" class="pri" id="b_pickfont"></button>
 <div id="q_font"></div>
 <p class="hint" id="h_font"></p>
</div>

<div id="p_wifi" class="box" style="display:none">
 <div id="wifiinfo" class="q"></div>
 <label id="l_ssid"></label>
 <input id="ssid" list="nets" autocomplete="off"><datalist id="nets"></datalist>
 <button onclick="scan()" id="b_scan"></button> <span id="scanmsg" class="hint"></span>
 <label id="l_pass" style="display:block;margin-top:10px"></label>
 <input id="pass" type="password">
 <button class="pri" onclick="saveWifi()" id="b_savewifi"></button>
 <button class="bad" onclick="forgetWifi()" id="b_forget"></button>
 <p class="hint" id="h_wifi"></p>
</div>

<button class="pri" style="width:100%;padding:13px" onclick="done()" id="b_done"></button>
<p class="hint" id="h_done"></p>
</div>
<script>
function T(e,z){return Z?z:e}
function $(i){return document.getElementById(i)}
function kb(n){return n>1048576?(n/1048576).toFixed(1)+' MB':Math.ceil(n/1024)+' KB'}
function esc(s){return s.replace(/[&<>"']/g,c=>'&#'+c.charCodeAt(0)+';')}
var dir='/music';
var L={t_music:T('Music','音樂'),t_bg:T('Backgrounds','背景動畫'),t_font:T('Title font','標題字型'),t_wifi:'WiFi',
 b_pick:T('Upload MP3s','上傳 MP3'),b_mkdir:T('New folder','新增資料夾'),drop_music:T('or drop MP3 files here','或把 MP3 拖放到這裡'),
 b_pickgif:T('Upload GIFs','上傳 GIF'),drop_bg:T('or drop GIF files here','或把 GIF 拖放到這裡'),
 h_bg:T('GIFs are saved as bg1.gif, bg2.gif… in upload order. Any size works: the player crops or shrinks them to fit when it restarts.','GIF 會依上傳次序儲存為 bg1.gif、bg2.gif…。任何尺寸都可以：播放器重新啟動時會自動裁切或縮小。'),
 b_pickfont:T('Upload font (.vlw)','上傳字型 (.vlw)'),
 h_font:T('Song titles and Chinese menus are drawn with this font. Make it on a computer with tools/gen_cjk_font.py.','歌名及中文選單使用此字型。請在電腦上用 tools/gen_cjk_font.py 製作。'),
 l_ssid:T('Network','網絡名稱'),l_pass:T('Password','密碼'),b_scan:T('Scan','掃描'),b_savewifi:T('Save and reconnect','儲存並重新連接'),
 b_forget:T('Forget WiFi','刪除 WiFi 設定'),h_wifi:T('2.4 GHz networks only. Saved on the player, not shown again.','只支援 2.4 GHz 網絡。密碼儲存在播放器內，不會再顯示。'),
 b_done:T('Done — restart the player','完成 — 重新啟動播放器'),h_done:T('New songs and backgrounds are prepared when the player restarts.','播放器重新啟動時會準備新歌曲及背景。')};
for(var k in L)$(k).textContent=L[k];

function tab(t){['music','bg','font','wifi'].forEach(x=>{$('p_'+x).style.display=x==t?'':'none';$('t_'+x).className=x==t?'on':''});
 if(t=='music')list();if(t=='bg')bglist();if(t=='font')fontinfo();if(t=='wifi')wifiinfo();}

function info(){fetch('/api/info').then(r=>r.json()).then(j=>{$('info').textContent=
 T('Free space ','剩餘空間 ')+kb(j.free)+' / '+kb(j.total)+'  ·  '+(j.ap?T('hotspot ','熱點 ')+j.ssid:j.ssid+'  '+j.ip)})}

function list(){fetch('/api/list?dir='+encodeURIComponent(dir)).then(r=>r.json()).then(j=>{
 var parts=dir.split('/').filter(x=>x),h='',p='';
 parts.forEach((x,i)=>{p+='/'+x;h+=(i?' / ':'')+'<span class="dir" onclick="go(\''+esc(p)+'\')">'+esc(x)+'</span>'});
 $('crumb').innerHTML=h;
 var o='';
 if(dir!='/music')o+='<div class="row"><span class="nm dir" onclick="go(\''+esc(dir.replace(/\/[^\/]*$/,''))+'\')">⬑ ..</span></div>';
 j.items.forEach(e=>{var full=dir+'/'+e.name;
  o+='<div class="row">'+(e.dir?'<span class="nm dir" onclick="go(\''+esc(full)+'\')">📁 '+esc(e.name)+'</span>'
   :'<span class="nm">'+esc(e.name)+'</span><span class="sz">'+kb(e.size)+'</span>')+
   '<button class="ic" title="rename" onclick="ren(\''+esc(full)+'\')">✎</button><button class="ic" title="delete" onclick="del(\''+esc(full)+'\','+(e.dir?1:0)+')">✕</button></div>'});
 if(!j.items.length)o+='<div class="hint">'+T('Empty','沒有檔案')+'</div>';
 $('list').innerHTML=o;})}
function go(d){dir=d;list()}

function bglist(){fetch('/api/list?dir=/bg').then(r=>r.json()).then(j=>{var o='';
 j.items.filter(e=>!e.dir).forEach(e=>{o+='<div class="row"><img class="th" src="/file?path='+encodeURIComponent('/bg/'+e.name)+'"><span class="nm">'+esc(e.name)+
  '</span><span class="sz">'+kb(e.size)+'</span><button class="ic" onclick="del(\'/bg/'+esc(e.name)+'\',0)">✕</button></div>'});
 $('bglist').innerHTML=o||'<div class="hint">'+T('No backgrounds yet','尚未有背景動畫')+'</div>';})}

function fontinfo(){fetch('/api/font').then(r=>r.json()).then(j=>{$('fontinfo').innerHTML=j.size?
 '<span class="ok">'+T('Installed','已安裝')+'</span> '+kb(j.size):'<span class="err">'+T('Not installed — titles show English letters only','未安裝 — 歌名只會顯示英文字母')+'</span>'})}

function wifiinfo(){fetch('/api/info').then(r=>r.json()).then(j=>{$('wifiinfo').textContent=j.ap?
 T('Using the player\'s own hotspot. Save a network to upload over your home WiFi.','正在使用播放器的熱點。儲存家中 WiFi 後可經家中網絡上傳。'):T('Connected to ','已連接到 ')+j.ssid;
 if(j.saved)$('ssid').value=j.saved;})}

function scan(){$('scanmsg').textContent=T('Scanning…','掃描中…');
 fetch('/api/scan').then(r=>r.json()).then(n=>{var d=$('nets');d.innerHTML='';n.forEach(s=>{var o=document.createElement('option');o.value=s;d.appendChild(o)});
 $('scanmsg').textContent=n.length+T(' found',' 個網絡')}).catch(()=>$('scanmsg').textContent=T('Scan failed','掃描失敗'))}

function post(u,b){return fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(b)}).then(r=>r.json())}
function saveWifi(){post('/api/wifi',{ssid:$('ssid').value,pass:$('pass').value}).then(j=>{
 document.body.innerHTML='<div class="w"><h1>CYD MP3</h1><p>'+T('Saved. The player is joining ','已儲存。播放器正在連接 ')+esc($('ssid').value)+
 T('. Its screen shows the new address to open.','。請在播放器螢幕上查看新的網址。')+'</p></div>'})}
function forgetWifi(){if(confirm(T('Forget the saved WiFi network?','刪除已儲存的 WiFi 設定？')))post('/api/wifi',{ssid:'',pass:''}).then(()=>
 document.body.innerHTML='<div class="w"><h1>CYD MP3</h1><p>'+T('Forgotten. The player restarts its hotspot.','已刪除。播放器會重新開啟熱點。')+'</p></div>')}
function del(p,isDir){if(!confirm(T('Delete ','刪除 ')+p+'?'))return;post('/api/delete',{path:p}).then(j=>{if(!j.ok)alert(j.error);
 if(p.startsWith('/bg/'))bglist();else list();info()})}
function ren(p){var old=p.split('/').pop(),n=prompt(T('New name','新名稱'),old);if(!n||n==old)return;
 post('/api/rename',{path:p,name:n}).then(j=>{if(!j.ok)alert(j.error);list()})}
function mkdir(){var n=prompt(T('Folder name','資料夾名稱'));if(!n)return;post('/api/mkdir',{path:dir+'/'+n}).then(j=>{if(!j.ok)alert(j.error);list()})}
function done(){post('/api/done',{}).then(()=>document.body.innerHTML='<div class="w"><h1>CYD MP3</h1><p>'+
 T('Restarting the player. You can close this page.','播放器正在重新啟動，可以關閉此頁面。')+'</p></div>')}

// GIF width and height live in bytes 6-9 of the file, little-endian.
function gifSize(f){return f.slice(0,10).arrayBuffer().then(b=>{var d=new DataView(b);
 if(String.fromCharCode(d.getUint8(0),d.getUint8(1),d.getUint8(2))!='GIF')return null;return [d.getUint16(6,true),d.getUint16(8,true)]})}

// One file at a time: the player has one SD card and a small buffer.
async function upload(files,target,qid,after){
 var q=$(qid);
 for(const f of files){
  var line=document.createElement('div');line.className='q';line.innerHTML=esc(f.name)+' <span class="sz">'+kb(f.size)+'</span><div class="bar"><i></i></div>';q.appendChild(line);
  if(target=='/bg'){var s=await gifSize(f);if(!s){line.innerHTML+='<span class="err">'+T('not a GIF','不是 GIF')+'</span>';continue}
   if(s[0]>2048){line.innerHTML+='<span class="err">'+T('too wide (max 2048 px)','太闊（最多 2048 px）')+'</span>';continue}}
  await new Promise(res=>{var x=new XMLHttpRequest(),fd=new FormData();fd.append('file',f,f.name);
   x.upload.onprogress=e=>{if(e.lengthComputable)line.querySelector('i').style.width=(100*e.loaded/e.total)+'%'};
   x.onload=()=>{var j={};try{j=JSON.parse(x.responseText)}catch(e){}
    line.innerHTML+=j.ok?'<span class="ok">✓ '+esc(j.name||'')+'</span>':'<span class="err">'+esc(j.error||('HTTP '+x.status))+'</span>';res()};
   x.onerror=()=>{line.innerHTML+='<span class="err">'+T('connection lost','連線中斷')+'</span>';res()};
   x.open('POST','/api/upload?dir='+encodeURIComponent(target));x.send(fd)});
 }
 after();info();
}
$('f_music').onchange=e=>upload([...e.target.files],dir,'q_music',list);
$('f_bg').onchange=e=>upload([...e.target.files],'/bg','q_bg',bglist);
$('f_font').onchange=e=>upload([...e.target.files],'/.sys','q_font',fontinfo);
function dropper(id,fn){var d=$(id);d.ondragover=e=>{e.preventDefault();d.classList.add('hot')};d.ondragleave=()=>d.classList.remove('hot');
 d.ondrop=e=>{e.preventDefault();d.classList.remove('hot');fn([...e.dataTransfer.files])}}
dropper('drop_music',f=>upload(f,dir,'q_music',list));
dropper('drop_bg',f=>upload(f,'/bg','q_bg',bglist));
tab('music');info();
</script></body></html>)HTML";
