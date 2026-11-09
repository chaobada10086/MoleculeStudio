using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Net;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.RegularExpressions;
using System.Threading;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Markup;
using System.Windows.Media;
using System.Web.Script.Serialization;

class OnlineInstaller {
 // Four component model: Core -> AIBasic -> AIAdvanced -> OCSR.
 // min_installer_protocol 2 means "understands the four component feed and receipts".
 const int Protocol=2;
 const int Count=4;
 const string ReleaseBase="https://github.com/chaobada10086/MoleculeStudio/releases/download/";
 const string ShortcutDescription="MoleculeStudio · 由安装器创建";

 class Package {
  public string Key,Name,Hash,Url,Version,Display,Description,Reason,RequiresCoreVersion;
  public long Bytes;
  public bool Optional,Available;
  public string[] Requires;
 }

 static readonly string[] Keys={"Core","AIBasic","AIAdvanced","OCSR"};
 static readonly string[] Titles={"核心工作台","AI 基础","AI 高级","图片识别"};
 static readonly string[] Descriptions={
  "结构绘图 · 化学命名 · 核磁预测 · 三维构象",
  "AI 助手 · 检索 · 对话画图；需配置自己的 AI 服务",
  "高级绘图插件；需先安装 AI 基础",
  "图片 / PDF 转可编辑结构 · 本地运行"};

 // Built-in fallback table. Only used when the online feed cannot be read.
 // P7 must replace the AIBasic/AIAdvanced entries after the real split packages are
 // uploaded (they are placeholders here on purpose: the split hashes depend on the
 // final AI build), and may adjust the OCSR release tag once the audited
 // 0.95-20261108 package is published.
 static Package[] packages=Builtin();

 static readonly string[] LegacyAIKeys={"AIBasic","AI"};

 static Package BuiltinPackage(string key,string version,string name,string hash,long bytes,string url,bool available,string reason){
  int index=Array.IndexOf(Keys,key);
  return new Package{Key=key,Version=version,Name=name,Hash=hash,Bytes=bytes,Url=url,Available=available,Reason=reason,
   Display=Titles[index],Description=Descriptions[index],Optional=key!="Core",
   Requires=key=="Core"?new string[0]:key=="AIAdvanced"?new[]{"AIBasic"}:new[]{"Core"},
   RequiresCoreVersion=key=="Core"?null:"0.95-20261107-canvas-fix"};
 }
 static Package[] Builtin(){
  string coreName="MoleculeStudio-0.95-20261108-Core-win64.zip";
  string basicName="MoleculeStudio-0.95-20261108-AIBasic-addon-win64.zip";
  string advancedName="MoleculeStudio-0.95-20261108-AIAdvanced-addon-win64.zip";
  string ocsrName="MoleculeStudio-0.95-20261108-OCSR-addon-win64.zip";
  return new[]{
   BuiltinPackage("Core","0.95-20261108",coreName,
    "58a1354cac26f5c863a79fab0e710888f264c8d23facaa16a9dab11a4754769b",1115233626L,
    ReleaseBase+"v0.9.6.0.0/"+coreName,true,null),
   BuiltinPackage("AIBasic","0.95-20261108",basicName,
    "c47ab5d456143342ca9efedd8535a0a7270fab13706a22be308cdb14e48ace78",75242L,
    ReleaseBase+"v0.9.6.0.0/"+basicName,true,null),
   BuiltinPackage("AIAdvanced","0.95-20261108",advancedName,
    "7a386c2464e678d642968c9b1315a02827797360fc851a709077a9cf17006389",794723178L,
    ReleaseBase+"v0.9.6.0.0/"+advancedName,true,null),
   BuiltinPackage("OCSR","0.95-20261108",ocsrName,
    "d9977a679f51852d95b3e4348304ad7d133d851397bc61fbb68dff970b032822",833407453L,
    ReleaseBase+"v0.9.6.0.0/"+ocsrName,true,null)};
 }
 static Window window; static TextBox folder; static TextBlock status; static ProgressBar bar;
 static Button start,scan,uninstall; static CheckBox mirror,desktopShortcut; static bool busy,managed; static string cache;
 static long saved; static bool cancelled;
 static Grid[] jobRows; static ProgressBar[] jobBars; static TextBlock[] jobInfos;
 static string[] jobZips=new string[Count]; static string testSource; static readonly object installGate=new object();
 static readonly Brush MutedB=new SolidColorBrush(Color.FromRgb(0x56,0x71,0x6C));
 static readonly Brush DoneB=new SolidColorBrush(Color.FromRgb(0x15,0x7D,0x70));
 static readonly Brush FailedB=new SolidColorBrush(Color.FromRgb(0xA0,0x47,0x32));
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool CreateHardLink(string name,string existing,IntPtr reserved);
 static JavaScriptSerializer Json(){return new JavaScriptSerializer{MaxJsonLength=int.MaxValue};}

 static string feedStatus="内置发布版本（尚未联网确认）"; static bool feedChecked;
 static string feedUpgrade;

 static bool IsKey(string key){return Array.IndexOf(Keys,key)>=0;}
 static bool IsProgramFile(string rel){string ext=Path.GetExtension(rel).ToLowerInvariant();return ext==".exe"||ext==".dll"||ext==".pyd"||ext==".node"||ext==".so"||ext==".dylib";}

 static Package[] ParseFeed(string text){
  Dictionary<string,object> root;
  try{root=Json().Deserialize<Dictionary<string,object>>(text);}catch(Exception error){throw new IOException("版本清单内容无效",error);}
  if(root==null)throw new IOException("版本清单内容无效");
  int schema=root.ContainsKey("schema")?Convert.ToInt32(root["schema"]):0;
  int protocol=root.ContainsKey("min_installer_protocol")?Convert.ToInt32(root["min_installer_protocol"]):0;
  if(schema!=1&&schema!=2)throw new IOException("版本清单需要更新安装器");
  if(protocol<1||protocol>Protocol)throw new IOException("版本清单需要更新安装器");
  if(schema==2&&protocol<2)throw new IOException("版本清单协议标记无效");
  if(!string.Equals(Text(root,"channel"),"stable",StringComparison.Ordinal))throw new IOException("版本清单需要更新安装器");
  var found=new Dictionary<string,Package>(StringComparer.Ordinal);var order=new List<string>();
  foreach(Dictionary<string,object> row in Rows(root)){
   string id=Text(row,"id");string key=schema==1?(id=="AI"?"AIBasic":id):id;
   if(schema==1&&id!="Core"&&id!="AI"&&id!="OCSR")throw new IOException("版本清单内容无效");
   if(schema==2&&!IsKey(key))throw new IOException("版本清单包含未知组件："+(id??"")); 
   if(string.IsNullOrEmpty(key)||found.ContainsKey(key))throw new IOException("版本清单包含重复组件");
   string name=Text(row,"name"),hash=Text(row,"sha256"),url=Text(row,"url"),version=Text(row,"version");
   if(string.IsNullOrEmpty(version)||string.IsNullOrEmpty(name)||name!=Path.GetFileName(name)||name.IndexOfAny(Path.GetInvalidFileNameChars())>=0||!name.EndsWith(".zip",StringComparison.OrdinalIgnoreCase))throw new IOException("版本清单内容无效");
   if(hash==null||!Regex.IsMatch(hash,"^[0-9a-f]{64}$"))throw new IOException("版本清单内容无效");
   if(url==null||!url.StartsWith(ReleaseBase,StringComparison.Ordinal)||!url.EndsWith("/"+name,StringComparison.Ordinal))throw new IOException("版本清单内容无效");
   long bytes=row.ContainsKey("bytes")?Convert.ToInt64(row["bytes"]):0;
   if(bytes<=0)throw new IOException("包大小无效");
   string[] requires;
   if(schema==2){
    var deps=new List<string>();
    if(row.ContainsKey("requires")&&row["requires"]!=null)foreach(object dep in (IEnumerable)row["requires"])deps.Add(dep as string);
    requires=deps.ToArray();
   }else requires=key=="Core"?new string[0]:new[]{"Core"};
   int index=Array.IndexOf(Keys,key);
   var p=new Package{Key=key,Name=name,Hash=hash,Url=url,Version=version,Bytes=bytes,Available=true,Optional=key!="Core",
    Display=Text(row,"display_name")??Titles[index],Description=Descriptions[index],Requires=requires,
    RequiresCoreVersion=Text(row,"requires_core_version")};
   found.Add(key,p);order.Add(key);
  }
  if(schema==1){
   if(order.Count!=3||!found.ContainsKey("Core")||!found.ContainsKey("AIBasic")||!found.ContainsKey("OCSR"))throw new IOException("版本清单内容无效");
   if(found["AIBasic"].RequiresCoreVersion!=found["Core"].Version||found["OCSR"].RequiresCoreVersion!=found["Core"].Version)throw new IOException("模组与核心版本不兼容");
   // Schema 1 describes the old three-part release. AI is read as AIBasic; the
   // advanced plugin package cannot be expressed there, so it stays unavailable
   // until a schema 2 feed is published.
   var advanced=new Package{Key="AIAdvanced",Version=found["AIBasic"].Version,Name="",Hash="",Url="",Bytes=0,
    Available=false,Reason="旧版三组件清单：AI 高级需要新版清单，暂不可用",Optional=true,
    Display=Titles[2],Description=Descriptions[2],Requires=new[]{"AIBasic"},RequiresCoreVersion=found["Core"].Version};
   return new[]{found["Core"],found["AIBasic"],advanced,found["OCSR"]};
  }
  if(order.Count!=4||!found.ContainsKey("Core")||!found.ContainsKey("AIBasic")||!found.ContainsKey("AIAdvanced")||!found.ContainsKey("OCSR"))throw new IOException("版本清单缺少必需组件");
  string coreVersion=found["Core"].Version;
  if(found["Core"].Requires.Length!=0)throw new IOException("版本清单依赖无效：核心工作台不能依赖其他组件");
  foreach(var p in found.Values){
   if(p.Key=="Core")continue;
   if(p.RequiresCoreVersion==null||p.RequiresCoreVersion!=coreVersion)throw new IOException("模组与核心版本不兼容");
   foreach(string dep in p.Requires)if(string.IsNullOrEmpty(dep)||!found.ContainsKey(dep)||dep==p.Key)throw new IOException("版本清单依赖无效："+p.Key);
  }
  if(Array.IndexOf(found["AIBasic"].Requires,"Core")<0)throw new IOException("版本清单依赖无效：AI 基础需要核心工作台");
  if(Array.IndexOf(found["AIAdvanced"].Requires,"AIBasic")<0)throw new IOException("版本清单缺少依赖：AI 高级需要 AI 基础");
  if(Array.IndexOf(found["OCSR"].Requires,"Core")<0)throw new IOException("版本清单依赖无效：图片识别需要核心工作台");
  foreach(var p in found.Values)if(p.Key!="Core"&&!Reaches(found,p.Key,"Core"))throw new IOException("版本清单依赖不完整："+p.Key);
  foreach(var p in found.Values)CheckCycles(found,p.Key,new HashSet<string>(),new HashSet<string>());
  return new[]{found["Core"],found["AIBasic"],found["AIAdvanced"],found["OCSR"]};
 }
 static string Text(Dictionary<string,object> map,string key){object value;return map.TryGetValue(key,out value)&&value!=null?(string)value:null;}
 static List<Dictionary<string,object>> Rows(Dictionary<string,object> root){
  var list=new List<Dictionary<string,object>>();
  if(!root.ContainsKey("components")||root["components"]==null)throw new IOException("版本清单内容无效");
  foreach(object row in (IEnumerable)root["components"]){var map=row as Dictionary<string,object>;if(map==null)throw new IOException("版本清单内容无效");list.Add(map);}
  return list;
 }
 static bool Reaches(Dictionary<string,Package> found,string from,string target){
  var stack=new Stack<string>();var seen=new HashSet<string>();stack.Push(from);
  while(stack.Count>0){string key=stack.Pop();if(!seen.Add(key))continue;
   foreach(string dep in found[key].Requires){if(dep==target)return true;if(found.ContainsKey(dep))stack.Push(dep);}}
  return from==target;
 }
 static void CheckCycles(Dictionary<string,Package> found,string key,HashSet<string> active,HashSet<string> done){
  if(done.Contains(key))return;
  if(!active.Add(key))throw new IOException("版本清单依赖存在循环");
  foreach(string dep in found[key].Requires)CheckCycles(found,dep,active,done);
  active.Remove(key);done.Add(key);
 }
 static void LoadFeed(){
  if(feedChecked)return;feedChecked=true;
  // Version metadata comes only from the official HTTPS origin. Package mirrors
  // cannot substitute their own checksum manifest.
  foreach(string feedUrl in new[]{"https://raw.githubusercontent.com/chaobada10086/MoleculeStudio/main/updates/stable.json","https://api.github.com/repos/chaobada10086/MoleculeStudio/contents/updates/stable.json?ref=main"})try{
   Report("正在获取在线发布信息……",-1);
   var req=(HttpWebRequest)WebRequest.Create(feedUrl);req.UserAgent="MoleculeStudio-Installer/2.0";req.Accept="application/vnd.github.raw+json";req.Timeout=8000;req.ReadWriteTimeout=8000;req.CachePolicy=new System.Net.Cache.RequestCachePolicy(System.Net.Cache.RequestCacheLevel.NoCacheNoStore);
   using(var response=req.GetResponse())using(var input=response.GetResponseStream())using(var bytes=new MemoryStream()){
    byte[] buffer=new byte[8192];int n;
    while((n=input.Read(buffer,0,buffer.Length))>0){if(bytes.Length+n>1048576)throw new IOException("版本清单过大");bytes.Write(buffer,0,n);}
    packages=ParseFeed(System.Text.Encoding.UTF8.GetString(bytes.ToArray()));
    feedStatus=FeedStatus();return;
   }
  }catch(IOException error){if(error.Message.IndexOf("安装器",StringComparison.Ordinal)>=0)feedUpgrade=error.Message;}catch{}
  packages=Builtin();
  feedStatus=feedUpgrade==null?"在线清单暂不可用 · 使用内置版本，未确认最新":"在线清单要求更新安装器（"+feedUpgrade+"） · 使用内置版本";
 }
 static string FeedStatus(){
  string text="在线版本 · 核心 "+packages[0].Version;
  if(!packages[2].Available)text+=" · AI 高级需新版清单";
  return text;
 }

 static bool Has(string root,Package p){
  foreach(string key in p.Key=="AIBasic"?LegacyAIKeys:new[]{p.Key})
   if(File.Exists(Path.Combine(root,".installer-"+key+".json"))||File.Exists(Path.Combine(root,"MANIFEST-"+key+".json")))return true;
  return false;
 }
 static Dictionary<string,string> Inventory(string root,Package p){
  foreach(string key in p.Key=="AIBasic"?LegacyAIKeys:new[]{p.Key}){
   string file=Path.Combine(root,".installer-"+key+".json");bool legacy=!File.Exists(file);if(legacy)file=Path.Combine(root,"MANIFEST-"+key+".json");
   if(!File.Exists(file))continue;
   var result=new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase);
   var data=Json().Deserialize<Dictionary<string,object>>(File.ReadAllText(file));
   foreach(var item in (Dictionary<string,object>)data["files"]){Safe(root,item.Key);result[item.Key]=legacy?(string)((Dictionary<string,object>)item.Value)["sha256"]:(string)item.Value;}
   return result;
  }
  return new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase);
 }
 static bool Current(string root,Package p){
  foreach(string key in p.Key=="AIBasic"?LegacyAIKeys:new[]{p.Key}){
   string receipt=Path.Combine(root,".installer-"+key+".json");
   if(File.Exists(receipt)){try{return (string)Json().Deserialize<Dictionary<string,object>>(File.ReadAllText(receipt))["packageHash"]==p.Hash;}catch{return false;}}
   string manifest=Path.Combine(root,"MANIFEST-"+key+".json");
   if(File.Exists(manifest)){try{return string.Equals((string)Json().Deserialize<Dictionary<string,object>>(File.ReadAllText(manifest))["version"],p.Version,StringComparison.OrdinalIgnoreCase);}catch{return false;}}
  }
  return false;
 }
 static int State(string root,Package p){
  if(!Has(root,p))return 0;
  try{
   var files=Inventory(root,p);if(files.Count==0)return 0;
   bool missing=false,payload=false;
   foreach(var f in files.Keys){bool exists=File.Exists(Safe(root,f));if(!exists)missing=true;else if(!f.StartsWith("MANIFEST-",StringComparison.OrdinalIgnoreCase)&&!f.StartsWith(".installer-",StringComparison.OrdinalIgnoreCase))payload=true;}
   if(!payload)return 0;if(missing)return 2;return Current(root,p)?1:4;
  }catch{return 3;}
 }
 static int[] states={0,0,0,0}; static CheckBox[] picks; static TextBlock[] badges;
 static bool CanOpen(string root){return State(root,packages[0])==1&&File.Exists(Path.Combine(root,"MoleculeStudio.exe"));}
 static void OpenButton(bool enabled){var button=(Button)window.FindName("OpenApp");bool present=states[0]==1&&File.Exists(Path.Combine(folder.Text,"MoleculeStudio.exe"));button.Visibility=present?Visibility.Visible:Visibility.Collapsed;button.IsEnabled=enabled&&present;}
 static bool CanInstall(int index,int core,int basic){return index==0||(core==1&&(index!=2||basic==1));}
 static void RequireCore(string root,int index){
  var p=packages[index];
  if(!p.Available)throw new IOException(p.Display+" 需要联网获取新版清单后才能安装。");
  if(index==0)return;
  if(State(root,packages[0])!=1)throw new IOException("请先完整安装当前版本的核心工作台。");
  if(index==2&&State(root,packages[1])!=1)throw new IOException("请先安装 AI 基础，再安装 AI 高级。");
 }
 static int[] Plan(bool[] selected,int[] actual,bool remove){
  var planned=new bool[Count];
  for(int i=0;i<Count;i++)planned[i]=selected[i]&&(remove?actual[i]==1:actual[i]!=1);
  if(remove){
   if(planned[0])for(int i=1;i<Count;i++)planned[i]=actual[i]==1;
   if(planned[1])planned[2]=actual[2]==1;
  }else{
   if(planned[2]&&actual[1]!=1)planned[1]=true;
   if((planned[1]||planned[2]||planned[3])&&actual[0]!=1)planned[0]=true;
   if(planned[0])for(int i=1;i<Count;i++)if(actual[i]==4)planned[i]=true;
  }
  var list=new List<int>();
  for(int j=0;j<Count;j++){int i=remove?Count-1-j:j;if(planned[i])list.Add(i);}
  return list.ToArray();
 }
 static bool[] Selection(){var selected=new bool[Count];for(int i=0;i<Count;i++)selected[i]=picks[i].IsChecked==true;return selected;}
 static string Names(int[] indexes){var names=new List<string>();foreach(int i in indexes)names.Add(Titles[i]);return string.Join("、",names.ToArray());}
 // One selection expresses one operation. Never infer two conflicting actions.
 // An exclusively outdated selection expresses an update, not an uninstall.
 static int SelectionMode(bool[] selected,int[] actual){
  int[] add=Plan(selected,actual,false),del=Plan(selected,actual,true);
  if(add.Length==0&&del.Length==0)return 0;
  if(add.Length>0&&del.Length==0)return 1;
  if(del.Length>0&&add.Length==0)return 2;
  bool onlyUpdates=true;
  for(int i=0;i<Count;i++)if(selected[i]&&actual[i]!=4)onlyUpdates=false;
  return onlyUpdates?1:0;
 }
 static void Buttons(){
  if(picks==null)return;
  bool[] selected=Selection();int[] add=Plan(selected,states,false),del=Plan(selected,states,true);int mode=SelectionMode(selected,states);
  start.IsEnabled=!busy&&mode==1;uninstall.IsEnabled=!busy&&mode==2;
  start.Content=mode==1?"安装 / 更新所选 ("+add.Length+")":"安装所选";
  uninstall.Content=mode==2?"卸载所选 ("+del.Length+")":"卸载所选";
  long selectedBytes=0;foreach(int i in add)if(packages[i].Available)selectedBytes+=packages[i].Bytes;
   ((TextBlock)window.FindName("SelectionInfo")).Text=mode==1?"将同时下载并安装："+Names(add)+" · 合计约 "+SizeText(selectedBytes):mode==2?"卸载顺序："+Names(del):add.Length==0&&del.Length==0?"请勾选需要操作的组件":"已混选安装项与卸载项，请分开选择；两个操作均已禁用。";
 }
 static string SizeText(long bytes){
  if(bytes>=1073741824L)return (bytes/1073741824.0).ToString("0.##")+" GB";
  if(bytes>=1048576L)return (bytes/1048576.0).ToString("0.#")+" MB";
  if(bytes>=1024L)return (bytes/1024.0).ToString("0.#")+" KB";
  return bytes+" B";
 }
 static string Hint(int index,int[] found){
  var p=packages[index];
  string text=p.Key=="Core"?"必备组件":p.Key=="AIAdvanced"?"可选组件 · 需先安装 AI 基础":"可选组件 · 依赖核心工作台";
  if(p.Key=="AIAdvanced"&&found[1]==1)text="可选组件 · 已满足 AI 基础依赖";
  if(!p.Available)return text+" · "+(p.Reason==null?"需要新版清单":p.Reason);
  return text+" · 发布版本 "+p.Version+" · 大小 "+SizeText(p.Bytes)+(found[index]==1?" · 已安装":"");
 }
 static void Cards(string root){
  int[] found=new int[Count];bool[] available=new bool[Count];
  for(int i=0;i<Count;i++){found[i]=State(root,packages[i]);available[i]=packages[i].Available;}
  window.Dispatcher.Invoke((Action)(()=>{
   states=found;managed=false;
   for(int i=0;i<Count;i++)if(found[i]!=0)managed=true;
   string[] labels={"未安装","已安装","安装不完整","清单异常","可更新"};
   for(int i=0;i<Count;i++){
    string label=found[i]==0?(available[i]?"未安装":"需新版清单"):labels[found[i]];
    badges[i].Text=label;
    badges[i].Foreground=(System.Windows.Media.Brush)new System.Windows.Media.BrushConverter().ConvertFromString(found[i]==1?"#157D70":found[i]==0?(available[i]?"#637B78":"#AC5B22"):"#AC5B22");
    ((TextBlock)window.FindName("Hint"+i)).Text=Hint(i,found);
    picks[i].IsEnabled=!busy&&(available[i]||found[i]!=0);
   }
   start.Visibility=Visibility.Visible;uninstall.Visibility=managed?Visibility.Visible:Visibility.Collapsed;OpenButton(!busy);Buttons();
  }));
 }
 static void EnableActions(bool enabled){
  scan.IsEnabled=enabled;folder.IsEnabled=mirror.IsEnabled=desktopShortcut.IsEnabled=enabled;
  ((Button)window.FindName("Browse")).IsEnabled=enabled;
  for(int i=0;i<Count;i++)picks[i].IsEnabled=enabled&&(packages[i].Available||states[i]!=0);
  OpenButton(enabled);Buttons();if(!enabled){start.IsEnabled=false;uninstall.IsEnabled=false;}
 }
 static void Detect(){if(busy)return;string root=folder.Text;Work(()=>{Recover(root);LoadFeed();Cards(root);EnsureShortcut(root);Report(feedStatus,100);});}
 static void Batch(bool remove){
  if(busy)return;string root=folder.Text;Cards(root);
  if(SelectionMode(Selection(),states)!=(remove?2:1)){Report("当前选择不能执行此操作，请只选安装项或只选卸载项。",0);return;}
  int[] plan=Plan(Selection(),states,remove);if(plan.Length==0)return;
  bool mirrors=mirror.IsChecked==true,shortcut=desktopShortcut.IsChecked==true;
  if(remove&&MessageBox.Show("将按顺序卸载："+Names(plan)+"。\n用户文件和修改过的文件会保留。是否继续？","确认卸载所选组件",MessageBoxButton.YesNo,MessageBoxImage.Question)!=MessageBoxResult.Yes)return;
  if(!remove)foreach(int i in plan)if(!packages[i].Available){Report(packages[i].Display+" 需要联网获取新版清单后才能安装。",0);return;}
  Work(()=>{
   EnsureClosed(root);Recover(root);int kept=0;
   if(remove){
    foreach(int i in plan)kept+=Remove(root,packages[i]);
    if(!Has(root,packages[0])){Shortcut(root,Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),true);if(File.Exists(LastRoot)&&File.ReadAllText(LastRoot)==Path.GetFullPath(root))File.Delete(LastRoot);}
    Report("所选组件已卸载 · 用户文件已保留"+(kept>0?"，另保留 "+kept+" 个修改文件":""),100);
   }else{
    InstallAll(root,plan,mirrors,shortcut,false);
   }
   window.Dispatcher.Invoke((Action)(()=>{for(int i=0;i<Count;i++)picks[i].IsChecked=false;}));
  });
 }

 static string Hash(Stream s){using(var h=SHA256.Create())return BitConverter.ToString(h.ComputeHash(s)).Replace("-","").ToLowerInvariant();}
 static string Hash(string path){using(var s=File.OpenRead(path))return Hash(s);}
 static string transactionRoot,backupRoot;static Dictionary<string,string> undo;static bool committed;
 static void Journal(){string path=Safe(transactionRoot,".installer-transaction.json"),temp=path+".tmp";File.WriteAllText(temp,Json().Serialize(new{backup=Path.GetFileName(backupRoot),committed=committed}));if(File.Exists(path))File.Replace(temp,path,null);else File.Move(temp,path);}
 static void BeginTransaction(string root){transactionRoot=Path.GetFullPath(root);Directory.CreateDirectory(transactionRoot);backupRoot=Safe(transactionRoot,".installer-backup-"+Guid.NewGuid().ToString("N"));NoLinks(transactionRoot,backupRoot);Directory.CreateDirectory(backupRoot);undo=new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase);committed=false;File.WriteAllText(Safe(transactionRoot,".installer-undo.jsonl"),"");Journal();}
 static void Track(string relative){if(undo==null)return;string p=Safe(transactionRoot,relative);NoLinks(transactionRoot,p);if(undo.ContainsKey(relative))return;string copy=null;if(File.Exists(p)){copy=undo.Count.ToString()+".bin";File.Copy(p,Safe(backupRoot,copy));}File.AppendAllText(Safe(transactionRoot,".installer-undo.jsonl"),Json().Serialize(new{path=relative,backup=copy})+"\n");undo.Add(relative,copy);}
 static void CleanupTransaction(){foreach(string backup in undo.Values)if(backup!=null){string p=Safe(backupRoot,backup);if(File.Exists(p))File.Delete(p);}if(Directory.Exists(backupRoot)&&Directory.GetFileSystemEntries(backupRoot).Length==0)Directory.Delete(backupRoot);File.Delete(Safe(transactionRoot,".installer-transaction.json"));File.Delete(Safe(transactionRoot,".installer-undo.jsonl"));undo=null;}
 static void CommitTransaction(){committed=true;Journal();CleanupTransaction();}
 static void RollbackTransaction(){if(undo==null)return;if(committed){CleanupTransaction();return;}Report("正在恢复更新前的文件……",-1);foreach(var item in undo){string p=Safe(transactionRoot,item.Key);NoLinks(transactionRoot,p);if(File.Exists(p))File.Delete(p);if(item.Value!=null){Directory.CreateDirectory(Path.GetDirectoryName(p));File.Copy(Safe(backupRoot,item.Value),p);}}CleanupTransaction();}
 static void Recover(string root){string path=Safe(root,".installer-transaction.json");if(!File.Exists(path))return;var d=Json().Deserialize<Dictionary<string,object>>(File.ReadAllText(path));string name=(string)d["backup"];if(!Regex.IsMatch(name,"^\\.installer-backup-[a-f0-9]{32}$"))throw new IOException("恢复记录无效，已停止操作");transactionRoot=Path.GetFullPath(root);backupRoot=Safe(root,name);undo=new Dictionary<string,string>();foreach(string line in File.ReadLines(Safe(root,".installer-undo.jsonl"))){var e=Json().Deserialize<Dictionary<string,object>>(line);string relative=(string)e["path"];Safe(root,relative);string copy=e["backup"] as string;if(copy!=null&&!Regex.IsMatch(copy,"^[0-9]+\\.bin$"))throw new IOException("恢复文件记录无效");undo.Add(relative,copy);}committed=(bool)d["committed"];if(committed)CleanupTransaction();else RollbackTransaction();}
 static string Safe(string root,string rel){string p=Path.GetFullPath(Path.Combine(root,rel.Replace('/',Path.DirectorySeparatorChar)));if(!p.StartsWith(Path.GetFullPath(root)+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase))throw new IOException("压缩包路径越界");if(p.Length>230&&!p.StartsWith(@"\\?\"))return p.StartsWith(@"\\")?@"\\?\UNC\"+p.Substring(2):@"\\?\"+p;return p;}
 static void CheckCancel(){if(cancelled)throw new OperationCanceledException("已取消。已安装文件保留；重新安装会跳过校验通过的文件。");}
 static void Report(string text,double value){if(window!=null)window.Dispatcher.BeginInvoke((Action)(()=>{status.Text=text;bar.IsIndeterminate=value<0;if(value>=0)bar.Value=value;}));else Console.WriteLine(text);}
 static string Mb(long bytes){if(bytes>=1073741824L)return ((double)bytes/1073741824.0).ToString("0.00")+" GB";if(bytes>=1048576L)return ((double)bytes/1048576.0).ToString("0.0")+" MB";return (bytes/1024L)+" KB";}
 static void JobReport(int index,string text,double value,Brush color){
  if(index<0){Report(text,value);return;}
  if(window==null){Console.WriteLine("["+index+"] "+text);return;}
  window.Dispatcher.BeginInvoke((Action)(()=>{if(index>=Count||jobInfos==null||jobInfos[index]==null)return;jobInfos[index].Text=text;jobInfos[index].Foreground=color;jobBars[index].IsIndeterminate=value<0;if(value>=0)jobBars[index].Value=value;}));
 }
 static void ShowJobs(int[] plan){
  if(window==null)return;
  window.Dispatcher.Invoke((Action)(()=>{
   for(int i=0;i<Count;i++){bool active=Array.IndexOf(plan,i)>=0;jobRows[i].Visibility=active?Visibility.Visible:Visibility.Collapsed;jobBars[i].IsIndeterminate=false;jobBars[i].Value=0;jobInfos[i].Foreground=MutedB;jobInfos[i].Text=active?"排队中":"";}
   bar.Visibility=Visibility.Collapsed;bar.IsIndeterminate=false;bar.Value=0;
  }));
 }
 static void HideJobs(){
  if(window==null)return;
  for(int i=0;i<Count;i++)jobRows[i].Visibility=Visibility.Collapsed;
  bar.Visibility=Visibility.Visible;
 }
 class DownloadProgress{
  public long Total,Done,LastDone,LastTick;public Stopwatch Watch=Stopwatch.StartNew();public readonly object Gate=new object();
  public void Report(int job,string label,int segments){
   lock(Gate){
    long now=Watch.ElapsedMilliseconds;if(now-LastTick<400)return;
    double seconds=Math.Max(0.001,(now-LastTick)/1000.0);double speed=(Done-LastDone)/1048576.0/seconds;
    double percent=Total>0?100.0*Done/Total:-1;
    string detail="下载 "+label+" · "+(Done/1048576)+" MB"+(Total>0?" / "+(Total/1048576)+" MB":"")+" · "+speed.ToString("0.0")+" MB/s"+(segments>1?" · "+segments+" 线程":"");
    JobReport(job,detail,percent,MutedB);
    LastDone=Done;LastTick=now;
   }
  }
 }
 static void NoLinks(string root,string file){string stop=Path.GetFullPath(root).TrimEnd('\\');string p=Path.GetDirectoryName(file);while(p!=null){if(Directory.Exists(p)&&(File.GetAttributes(p)&FileAttributes.ReparsePoint)!=0)throw new IOException("安装目录包含目录链接，已停止移除："+p);if(string.Equals(p.TrimEnd('\\'),stop,StringComparison.OrdinalIgnoreCase))break;p=Path.GetDirectoryName(p);}}
 static void EnsureClosed(string root){string prefix=Path.GetFullPath(root).TrimEnd('\\')+"\\";foreach(var process in Process.GetProcesses())using(process){string exe=null;try{exe=process.MainModule.FileName;}catch{}if(exe!=null&&exe.StartsWith(prefix,StringComparison.OrdinalIgnoreCase))throw new IOException("请先关闭此安装目录中的程序，再更改组件或卸载。");}}

 static int Remove(string root,Package package){
  if(package.Key=="Core"){
   var installed=new List<string>();
   foreach(var other in packages)if(other.Key!="Core"&&Has(root,other))installed.Add(other.Display);
   if(installed.Count>0)throw new IOException("请先卸载 "+string.Join("、",installed.ToArray())+"，再卸载核心工作台。");
  }
  root=Path.GetFullPath(root);EnsureClosed(root);
  var files=Inventory(root,package);var shared=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
  foreach(var other in packages)if(other.Key!=package.Key)foreach(var f in Inventory(root,other).Keys)shared.Add(f);
  // Preflight all paths before deleting anything. Never recursively delete an installation tree.
  foreach(var rel in files.Keys)NoLinks(root,Safe(root,rel));
  int preserved=0,n=0;var dirs=new HashSet<string>();
  foreach(var item in files){
   CheckCancel();if(shared.Contains(item.Key))continue;string p=Safe(root,item.Key);
   if(File.Exists(p)){if(FileMatches(p,item.Value)||ValidGeneratedConfig(root,package,item.Key)){File.Delete(p);dirs.Add(Path.GetDirectoryName(p));}else preserved++;}
   if(++n%200==0)Report("移除 "+package.Display+" · "+n+" / "+files.Count,100.0*n/files.Count);
  }
  foreach(string key in package.Key=="AIBasic"?LegacyAIKeys:new[]{package.Key})
   foreach(string prefix in new[]{".installer-","MANIFEST-"}){string metadata=Safe(root,prefix+key+".json");if(File.Exists(metadata))File.Delete(metadata);}
  // Only remove empty directories, and only ancestors of files owned by this component.
  foreach(string dir in dirs){string d=dir;while(d!=null&&!string.Equals(d.TrimEnd('\\'),root.TrimEnd('\\'),StringComparison.OrdinalIgnoreCase)&&d.StartsWith(root+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase)){
   if(!Directory.Exists(d)||Directory.GetFileSystemEntries(d).Length!=0)break;Directory.Delete(d);d=Path.GetDirectoryName(d);}}
  return preserved;
 }
 static bool GeneratedConfig(Package package,string rel){return package.Key=="OCSR"&&string.Equals(rel.Replace('\\','/'),"ocsr_runtime/venv/pyvenv.cfg",StringComparison.OrdinalIgnoreCase);}
 static string RuntimeConfig(string root){return "home = "+Path.GetFullPath(Path.Combine(root,"ocsr_runtime/python/cpython-3.10-windows-x86_64-none"))+"\ninclude-system-site-packages = false\nversion_info = 3.10\n";}
 static string NormalizeConfig(string text){var lines=new List<string>();foreach(string line in text.TrimStart('\uFEFF').Replace("\r","").Split('\n'))if(line.Trim().Length>0)lines.Add(line.Trim());return string.Join("\n",lines.ToArray());}
 static bool ValidGeneratedConfig(string root,Package p,string rel){if(!GeneratedConfig(p,rel))return false;try{return string.Equals(NormalizeConfig(File.ReadAllText(Safe(root,rel))),NormalizeConfig(RuntimeConfig(root)),StringComparison.OrdinalIgnoreCase);}catch{return false;}}

 static bool FileMatches(string path,string expected){
  if(string.IsNullOrEmpty(expected))return false;
  if(expected.StartsWith("size:",StringComparison.Ordinal)){long size;return long.TryParse(expected.Substring(5),out size)&&new FileInfo(path).Length==size;}
  return string.Equals(Hash(path),expected,StringComparison.OrdinalIgnoreCase);
 }
 static bool Verify(string root,Package p){
  string receipt=null;bool legacy=true;
  foreach(string key in p.Key=="AIBasic"?LegacyAIKeys:new[]{p.Key}){
   string own=Path.Combine(root,".installer-"+key+".json");
   if(File.Exists(own)){receipt=own;legacy=false;break;}
   string manifest=Path.Combine(root,"MANIFEST-"+key+".json");
   if(File.Exists(manifest)){receipt=manifest;legacy=true;break;}
  }
  if(receipt==null)return false;
  var data=Json().Deserialize<Dictionary<string,object>>(File.ReadAllText(receipt));
  if(!legacy&&(string)data["packageHash"]!=p.Hash)return false;
  if(legacy&&!Current(root,p))return false;
  var files=(Dictionary<string,object>)data["files"];
  var keys=new List<string>();var expectedMap=new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase);
  foreach(var entry in files){keys.Add(entry.Key);expectedMap[entry.Key]=legacy?(string)((Dictionary<string,object>)entry.Value)["sha256"]:(string)entry.Value;}
  int index=0,failed=0,done=0;Exception failure=null;object gate=new object();
  int workers=Math.Max(2,Math.Min(16,Environment.ProcessorCount));
  var threads=new Thread[workers];
  for(int w=0;w<workers;w++){
   threads[w]=new Thread(()=>{
    try{
     while(true){
      if(failed!=0)return;
      int i=Interlocked.Increment(ref index)-1;if(i>=keys.Count)return;
      CheckCancel();string rel=keys[i];string f=Safe(root,rel);
      if(!File.Exists(f)||(!FileMatches(f,expectedMap[rel])&&!ValidGeneratedConfig(root,p,rel))){Interlocked.Exchange(ref failed,1);return;}
      int finished=Interlocked.Increment(ref done);
      if(finished%300==0)Report("正在校验已安装的 "+p.Display+" · "+finished+" / "+keys.Count,100.0*finished/keys.Count);
     }
    }catch(Exception error){lock(gate){if(failure==null)failure=error;}}
   });
   threads[w].IsBackground=true;threads[w].Start();
  }
  foreach(var thread in threads)thread.Join();
  if(failure!=null)throw failure;
  return failed==0;
 }
 static string Download(Package p,bool mirrors,int job=-1){
  if(!p.Available||string.IsNullOrEmpty(p.Hash)||p.Bytes<=0)throw new IOException(p.Display+" 需要联网获取新版清单后才能安装。");
  if(testSource!=null){string local=Path.Combine(testSource,p.Name);if(File.Exists(local))return local;}
  string path=Path.Combine(cache,p.Name);if(File.Exists(path)){JobReport(job,"复用已校验的压缩包 · "+p.Display,-1,MutedB);if(CachedHash(path,job,p.Display)==p.Hash)return path;}
  string url=p.Url;var sources=new List<string>();
  if(mirrors){sources.Add("https://ghfast.top/"+url);sources.Add("https://gh-proxy.com/"+url);sources.Add("https://ghproxy.net/"+url);}
  sources.Add(url);
  string partial=path+".part";var failures=new List<string>();
  foreach(string source in sources)for(int attempt=0;attempt<3;attempt++){
   CheckCancel();string host=new Uri(source).Host;
   try{
    if(File.Exists(partial))try{File.Delete(partial);}catch{}
    JobReport(job,"连接 "+host+" · "+p.Display+(attempt>0?" · 第 "+(attempt+1)+" 次尝试":""),-1,MutedB);
    DownloadFile(source,partial,job,p.Display);
    JobReport(job,"校验压缩包 SHA-256 · "+p.Display,-1,MutedB);
    if(HashFileProgress(partial,job,p.Display)!=p.Hash)throw new IOException("SHA-256 校验不匹配");
    if(File.Exists(path))File.Delete(path);
    File.Move(partial,path);
    try{var info=new FileInfo(path);File.WriteAllText(path+".sha256",p.Hash+" "+info.Length+" "+info.LastWriteTimeUtc.Ticks);}catch{}
    return path;
   }catch(OperationCanceledException){throw;}
   catch(Exception error){
    string reason=error.Message==null?"未知错误":error.Message.Replace("\r"," ").Replace("\n"," ");
    if(reason.Length>140)reason=reason.Substring(0,140)+"…";
    failures.Add(host+"："+reason);
    bool lastAttemptForSource=attempt==2;
    JobReport(job,"来源 "+host+" 失败（"+reason+"），"+(lastAttemptForSource?"切换下一个来源…":"重试…"),-1,MutedB);
    System.Threading.Thread.Sleep(250);
   }
  }
  var unique=new List<string>();
  foreach(string item in failures)if(!unique.Contains(item))unique.Add(item);
  throw new IOException("所有下载来源均失败，请检查网络/代理后重试（已在 "+sources.Count+" 个来源间自动切换）："+string.Join("；",unique.ToArray()));
 }
 static void CopyWithProgress(Stream input,Stream output,DownloadProgress progress,int job,string label,int segments){
  byte[] buffer=new byte[262144];long last=0;int n;
  while((n=input.Read(buffer,0,buffer.Length))>0){
   CheckCancel();output.Write(buffer,0,n);Interlocked.Add(ref progress.Done,n);
   long now=progress.Watch.ElapsedMilliseconds;if(now-last>400){progress.Report(job,label,segments);last=now;}
  }
 }
 static void DownloadFile(string source,string partial,int job,string label){
  const int MaxSegments=4;const long Chunk=8L*1024*1024;
  long total=-1;
  var probe=(HttpWebRequest)WebRequest.Create(source);
  probe.UserAgent="MoleculeStudio-Installer/2.0";probe.Timeout=20000;probe.ReadWriteTimeout=30000;
  probe.AddRange(0,Chunk-1);
  using(var response=probe.GetResponse()){
   var http=response as HttpWebResponse;string range=response.Headers["Content-Range"];
   if(http!=null&&(int)http.StatusCode==206&&!string.IsNullOrEmpty(range)){
    int slash=range.LastIndexOf('/');long parsed;if(slash>=0&&long.TryParse(range.Substring(slash+1),out parsed))total=parsed;
   }
   if(total<=0){
    var single=new DownloadProgress{Total=response.ContentLength};
    using(var input=response.GetResponseStream())using(var output=File.Create(partial))CopyWithProgress(input,output,single,job,label,1);
    return;
   }
   if(total<=Chunk){
    var single=new DownloadProgress{Total=total};
    using(var input=response.GetResponseStream())using(var output=File.Create(partial))CopyWithProgress(input,output,single,job,label,1);
    if(new FileInfo(partial).Length!=total)throw new IOException("下载长度不完整");
    return;
   }
  }
  int segments=(int)Math.Min(MaxSegments,(total+Chunk-1)/Chunk);
  if(segments<2)segments=2;
  long baseSize=total/segments,remainder=total%segments;
  long[] starts=new long[segments];long[] sizes=new long[segments];
  long offset=0;
  for(int i=0;i<segments;i++){sizes[i]=baseSize+(i<remainder?1:0);starts[i]=offset;offset+=sizes[i];}
  var progress=new DownloadProgress{Total=total};
  using(var file=File.Create(partial))file.SetLength(total);
  Exception failure=null;var gate=new object();var threads=new Thread[segments];
  for(int i=0;i<segments;i++){
   int index=i;
   threads[i]=new Thread(()=>{
    try{
     var request=(HttpWebRequest)WebRequest.Create(source);request.UserAgent="MoleculeStudio-Installer/2.0";request.Timeout=20000;request.ReadWriteTimeout=30000;
     request.AddRange(starts[index],starts[index]+sizes[index]-1);
     using(var resp=request.GetResponse())using(var input=resp.GetResponseStream())using(var stream=new FileStream(partial,FileMode.Open,FileAccess.Write,FileShare.ReadWrite)){
      stream.Seek(starts[index],SeekOrigin.Begin);byte[] buffer=new byte[262144];long written=0;int n;long last=0;
      while((n=input.Read(buffer,0,buffer.Length))>0){
       CheckCancel();stream.Write(buffer,0,n);written+=n;Interlocked.Add(ref progress.Done,n);
       long now=progress.Watch.ElapsedMilliseconds;if(now-last>400){progress.Report(job,label,segments);last=now;}
      }
      if(written!=sizes[index])throw new IOException("分段长度不完整");
     }
    }catch(Exception error){lock(gate){if(failure==null)failure=error;}}
   });
   threads[i].IsBackground=true;threads[i].Start();
  }
  foreach(var thread in threads)thread.Join();
  if(failure!=null)throw failure;
  if(new FileInfo(partial).Length!=total)throw new IOException("下载长度不完整");
 }

 static string Duplicate(string root,string rel){
  const string a="ocsr_runtime/python/cpython-3.10-windows-x86_64-none/",b="ocsr_runtime/venv/Lib/site-packages/";
  if(rel.StartsWith(a))return Safe(root,"chemistry_python/"+rel.Substring(a.Length));
  if(rel.StartsWith(b))return Safe(root,"chemistry_python/Lib/site-packages/"+rel.Substring(b.Length));
  return null;
 }
 static string HashStream(Stream input){using(var sha=SHA256.Create())return BitConverter.ToString(sha.ComputeHash(input)).Replace("-","").ToLowerInvariant();}
 static string HashWhileCopy(Stream input,Stream output){
  using(var sha=SHA256.Create()){
   byte[] buffer=new byte[262144];int n;
   while((n=input.Read(buffer,0,buffer.Length))>0){sha.TransformBlock(buffer,0,n,buffer,0);output.Write(buffer,0,n);}
   sha.TransformFinalBlock(new byte[0],0,0);
   return BitConverter.ToString(sha.Hash).Replace("-","").ToLowerInvariant();
  }
 }
 static Dictionary<string,string> ManifestHashes(ZipArchive z,Package package){
  var map=new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase);
  var entry=z.GetEntry("MoleculeStudio/MANIFEST-"+package.Key+".json");
  if(entry==null)return map;
  try{
   using(var stream=entry.Open())using(var reader=new StreamReader(stream,System.Text.Encoding.UTF8)){
    var data=Json().Deserialize<Dictionary<string,object>>(reader.ReadToEnd());
    if(data!=null&&data.ContainsKey("files"))foreach(var item in (Dictionary<string,object>)data["files"]){
     object value=item.Value;
     if(value is string)map[item.Key]=(string)value;
     else if(value is Dictionary<string,object>){var nested=(Dictionary<string,object>)value;if(nested.ContainsKey("sha256"))map[item.Key]=(string)nested["sha256"];}
    }
   }
  }catch{}
  return map;
 }
 static string HashFileProgress(string path,int job,string label){
  var info=new FileInfo(path);long total=info.Length;byte[] buffer=new byte[1048576];long done=0,last=0;
  using(var sha=SHA256.Create()){
   using(var stream=File.OpenRead(path)){
    int n;var watch=Stopwatch.StartNew();
    while((n=stream.Read(buffer,0,buffer.Length))>0){
     sha.TransformBlock(buffer,0,n,buffer,0);done+=n;
     long now=watch.ElapsedMilliseconds;
     if(now-last>300){JobReport(job,"校验压缩包 · "+label+" · "+(done/1048576)+" MB / "+(total/1048576)+" MB",total>0?100.0*done/total:-1,MutedB);last=now;}
    }
   }
   sha.TransformFinalBlock(new byte[0],0,0);
   return BitConverter.ToString(sha.Hash).Replace("-","").ToLowerInvariant();
  }
 }
 static string CachedHash(string path,int job,string label){
  string meta=path+".sha256";var info=new FileInfo(path);
  if(File.Exists(meta)){try{var parts=File.ReadAllText(meta).Split(' ');if(parts.Length==3&&long.Parse(parts[1])==info.Length&&long.Parse(parts[2])==info.LastWriteTimeUtc.Ticks)return parts[0];}catch{}}
  string hash=HashFileProgress(path,job,label);
  try{File.WriteAllText(meta,hash+" "+info.Length+" "+info.LastWriteTimeUtc.Ticks);}catch{}
  return hash;
 }
 static void Extract(string zip,string root,Package package){
  var previous=Inventory(root,package);var records=new Dictionary<string,string>();
  using(var z=ZipFile.OpenRead(zip)){
   int n=0,total=z.Entries.Count;
   foreach(var e in z.Entries){
    CheckCancel();if(e.FullName.EndsWith("/"))continue;
    if(!e.FullName.StartsWith("MoleculeStudio/"))throw new IOException("未知压缩包结构");
    string rel=e.FullName.Substring(15),dest=Safe(root,rel);
    if(rel.StartsWith(".installer-",StringComparison.OrdinalIgnoreCase)||rel.StartsWith("MANIFEST-AI.json",StringComparison.OrdinalIgnoreCase)&&package.Key=="AIBasic")throw new IOException("发布包包含保留的安装器记录路径");
    Directory.CreateDirectory(Path.GetDirectoryName(dest));
    string received="size:"+e.Length;
    if(GeneratedConfig(package,rel)){
     Track(rel);if(File.Exists(dest))File.Delete(dest);File.WriteAllText(dest,RuntimeConfig(root),new System.Text.UTF8Encoding(false));records[rel]="size:"+new FileInfo(dest).Length;n++;continue;
    }
    bool generatedBytecode=rel.EndsWith(".pyc",StringComparison.OrdinalIgnoreCase)||rel.IndexOf("/__pycache__/",StringComparison.OrdinalIgnoreCase)>=0;
    if(generatedBytecode&&File.Exists(dest)){n++;continue;}
    // The ZIP itself is verified by SHA-256 before extraction, so per-file hashing is
    // unnecessary here: same-size files are left alone, changed or program files are
    // written inside the transaction (backup available for rollback).
    if(File.Exists(dest)&&!IsProgramFile(rel)&&new FileInfo(dest).Length==e.Length){records[rel]=received;n++;continue;}
    Track(rel);if(File.Exists(dest))File.Delete(dest);
    string duplicate=Duplicate(root,rel);bool linked=false;
    if(duplicate!=null&&File.Exists(duplicate)&&new FileInfo(duplicate).Length==e.Length)linked=CreateHardLink(dest,duplicate,IntPtr.Zero);
    if(linked){saved+=e.Length;}
    else{
     string temp=dest+".installing";
     using(var input=e.Open())using(var output=File.Create(temp))input.CopyTo(output);
     File.Move(temp,dest);
    }
    records[rel]=received;
    if(++n%200==0)Report("安装 "+package.Display+" · "+n+" / "+total,100.0*n/total);
   }
  }
  if(undo!=null){
   var shared=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
   foreach(var other in packages)if(other.Key!=package.Key)foreach(var f in Inventory(root,other).Keys)shared.Add(f);
   foreach(var old in previous){string f=Safe(root,old.Key);if(!records.ContainsKey(old.Key)&&!shared.Contains(old.Key)&&File.Exists(f)&&FileMatches(f,old.Value)){Track(old.Key);File.Delete(f);}}
  }
  Track(".installer-"+package.Key+".json");
  File.WriteAllText(Path.Combine(root,".installer-"+package.Key+".json"),Json().Serialize(new{packageHash=package.Hash,version=package.Version,files=records}));
  if(package.Key=="AIBasic")foreach(string legacy in new[]{".installer-AI.json","MANIFEST-AI.json"}){
   string stale=Safe(root,legacy);
   if(File.Exists(stale)){Track(legacy);File.Delete(stale);}
  }
 }
 static void InstallAll(string root,int[] plan,bool mirrors,bool shortcut,bool health){
  var archives=new string[Count];var errors=new Exception[Count];BeginTransaction(root);
  try{
   foreach(int i in plan){
    var dependency=packages[i];
    if(!dependency.Available)throw new IOException(dependency.Display+" 需要联网获取新版清单后才能安装。");
    if(i!=0&&State(root,packages[0])!=1&&Array.IndexOf(plan,0)<0)throw new IOException("请先完整安装当前版本的核心工作台。");
    if(i==2&&State(root,packages[1])!=1&&Array.IndexOf(plan,1)<0)throw new IOException("请先安装 AI 基础，再安装 AI 高级。");
   }
   ShowJobs(plan);
   Report("正在同时下载并安装 "+Names(plan)+" ……",-1);
   var threads=new Thread[plan.Length];
   for(int i=0;i<plan.Length;i++){
    int index=plan[i];
    threads[i]=new Thread(()=>{
     try{archives[index]=Download(packages[index],mirrors,index);}
     catch(Exception error){errors[index]=error;JobReport(index,"失败："+error.Message,0,FailedB);}
    });
    threads[i].IsBackground=true;threads[i].Start();
   }
   foreach(var thread in threads)thread.Join();
   foreach(int i in plan)if(errors[i]!=null)throw errors[i];
   foreach(int i in plan){
    JobReport(i,"准备安装……",0,MutedB);
    Extract(archives[i],root,packages[i]);
    if(health){JobReport(i,"校验运行时环境……",-1,MutedB);Health(root,packages[i],true);}
    JobReport(i,"已安装 · 完成",100,DoneB);
   }
   CommitTransaction();
  }catch{RollbackTransaction();throw;}
  File.WriteAllText(LastRoot,Path.GetFullPath(root));
  if(shortcut)Shortcut(root,Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),false);
  foreach(var file in archives)if(file!=null&&File.Exists(file))File.Delete(file);
  Report("已完成安装："+Names(plan)+(shortcut?" · 桌面快捷方式已就绪":""),100);
 }
 static void EnsureShortcut(string root){
  try{
   if(desktopShortcut==null||desktopShortcut.IsChecked!=true)return;
   if(State(root,packages[0])!=1)return;
   string desktop=Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);
   if(!File.Exists(ShortcutPath(root,desktop,false)))Shortcut(root,desktop,false);
  }catch{}
 }
 static void CoreHealth(string root){
  string exe=Path.Combine(root,"chemistry_python/python.exe");if(!File.Exists(exe))throw new IOException("缺少化学解释器");
  var si=new ProcessStartInfo(exe,"-B -c \"from rdkit.Chem import AllChem,rdDistGeom;import torch,scipy,pandas;import sys;sys.path.append('chemistry_runtime/nmr-ml-site');from csp5 import predict_mols\""){UseShellExecute=false,CreateNoWindow=true,WorkingDirectory=root,RedirectStandardError=true};
  using(var p=Process.Start(si)){string error="";p.ErrorDataReceived+=(s,e)=>{if(e.Data!=null)error+=e.Data+"\n";};p.BeginErrorReadLine();if(!p.WaitForExit(120000)){p.Kill();throw new IOException("运行时自检超时");}p.WaitForExit();if(p.ExitCode!=0)throw new IOException("运行时自检失败，可能被系统策略拦截：\n"+error);}
 }
 static void AIHealth(string root,Package package){
  if(package.Key=="AIBasic"){
   foreach(string rel in new[]{"ai_assistant.py","ai_attachments.py","ai_library.py","ai_local_sources.py","ai_ocsr_bridge.py","ai_ring_catalog.json","ai_scene.py"})
    if(!File.Exists(Safe(root,rel)))throw new IOException("缺少 AI 基础文件："+rel);
   return;
  }
  string node=Path.Combine(root,"ai","node.exe");if(!File.Exists(node))throw new IOException("缺少 AI 运行时：ai/node.exe");
  var si=new ProcessStartInfo(node,"-v"){UseShellExecute=false,CreateNoWindow=true,WorkingDirectory=root,RedirectStandardError=true,RedirectStandardOutput=true};
  using(var p=Process.Start(si)){string error="";p.ErrorDataReceived+=(s,e)=>{if(e.Data!=null)error+=e.Data+"\n";};p.BeginErrorReadLine();if(!p.WaitForExit(30000)){p.Kill();throw new IOException("AI 运行时自检超时");}p.WaitForExit();if(p.ExitCode!=0)throw new IOException("AI 运行时自检失败：\n"+error);}
  foreach(string rel in new[]{"ai_plugins/plugin.xml","ai_plugins/draw_plugin.py","ai_plugins/biomolecules.py","ai_plugins/__init__.py","ai_dsh.py","dsh_proxy.py"})
   if(!File.Exists(Safe(root,rel)))throw new IOException("缺少 AI 高级文件："+rel);
 }
 static void Health(string root,Package package,bool justExtracted=false){
  if(package.Key=="Core"){CoreHealth(root);return;}
  if(package.Key=="AIBasic"||package.Key=="AIAdvanced"){AIHealth(root,package);return;}
  if(justExtracted)return;
  if(!Verify(root,package))throw new IOException("图片识别文件校验失败，请重新安装。");
 }
 static void Work(Action action){
  if(busy)return;string root=folder.Text;busy=true;cancelled=false;HideJobs();EnableActions(false);
  new Thread(()=>{
   try{action();}catch(Exception error){Report(error.Message,0);}
   finally{
    try{Cards(root);}catch(Exception refreshError){
     window.Dispatcher.Invoke((Action)(()=>{
      states=new[]{0,0,0,0};managed=false;
      foreach(var badge in badges)badge.Text="需要重新检测";
      ((Button)window.FindName("OpenApp")).Visibility=Visibility.Collapsed;
     }));
     Report("状态刷新失败，请重新检测："+refreshError.Message,0);
    }
    window.Dispatcher.BeginInvoke((Action)(()=>{busy=false;EnableActions(true);}));
   }
  }){IsBackground=true}.Start();
 }

 static string LastRoot{get{return Path.Combine(Path.GetDirectoryName(cache),"installation.txt");}}

 static object ComGet(object obj,string name){return obj.GetType().InvokeMember(name,BindingFlags.GetProperty,null,obj,null);}
 static void ComSet(object obj,string name,object value){obj.GetType().InvokeMember(name,BindingFlags.SetProperty,null,obj,new object[]{value});}
 static string ShortcutPath(string root,string desktop,bool alternate){
  string suffix="";if(alternate){using(var bytes=new MemoryStream(System.Text.Encoding.UTF8.GetBytes(Path.GetFullPath(root).ToUpperInvariant())))suffix=" ("+Hash(bytes).Substring(0,8)+")";}
  return Path.Combine(desktop,"MoleculeStudio"+suffix+".lnk");
 }
 static void Shortcut(string root,string desktop,bool remove){
  root=Path.GetFullPath(root);string target=Path.Combine(root,"MoleculeStudio.exe");if(!remove&&!File.Exists(target))throw new IOException("创建快捷方式失败：未找到 MoleculeStudio.exe");
  Directory.CreateDirectory(desktop);object shell=Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell"));
  try{
   for(int i=0;i<2;i++){
    string path=ShortcutPath(root,desktop,i!=0);bool exists=File.Exists(path);if(remove&&!exists)continue;
    object link=shell.GetType().InvokeMember("CreateShortcut",BindingFlags.InvokeMethod,null,shell,new object[]{path});
    try{
     bool same=string.Equals((string)ComGet(link,"TargetPath"),target,StringComparison.OrdinalIgnoreCase);
     if(remove){if(same&&(string)ComGet(link,"Description")==ShortcutDescription)File.Delete(path);continue;}
     if(exists){if(same)return;if(i==0)continue;throw new IOException("桌面存在同名快捷方式，已保留，请手动处理："+path);}
     ComSet(link,"TargetPath",target);ComSet(link,"WorkingDirectory",root);ComSet(link,"IconLocation",target+",0");ComSet(link,"Description",ShortcutDescription);link.GetType().InvokeMember("Save",BindingFlags.InvokeMethod,null,link,null);return;
    }finally{Marshal.FinalReleaseComObject(link);}
   }
  }finally{Marshal.FinalReleaseComObject(shell);}
 }

 // Layout: the window never exceeds the work area; cards reflow to one column on
 // narrow windows; the content area scrolls while the status and actions stay fixed.
 static void FitWindow(){
  var area=SystemParameters.WorkArea;
  double w=Math.Min(900.0,area.Width-24.0);if(w<560.0)w=Math.Min(560.0,area.Width-8.0);
  double h=Math.Min(850.0,area.Height-24.0);if(h<460.0)h=Math.Min(460.0,area.Height-8.0);
  window.Width=w;window.Height=h;window.Left=area.Left+(area.Width-w)/2.0;window.Top=area.Top+(area.Height-h)/2.0;
 }
 static void LayoutForSize(){
  if(window==null)return;
  double w=window.ActualWidth>0?window.ActualWidth:window.Width;
  var cards=(UniformGrid)window.FindName("CardGrid");if(cards!=null)cards.Columns=w>=560.0?2:1;
  ((Grid)window.FindName("Root")).Margin=w>=760.0?new Thickness(28,22,28,16):new Thickness(16,14,16,10);
  var title=(TextBlock)window.FindName("HeaderTitle");if(title!=null)title.FontSize=w>=760.0?30.0:24.0;
 }
 static Rect Bounds(FrameworkElement element){return element.TransformToAncestor(window).TransformBounds(new Rect(element.RenderSize));}
 static bool Inside(FrameworkElement element){var r=Bounds(element);return element.IsVisible&&r.Width>0&&r.Height>0&&r.Left>=-1.0&&r.Top>=-1.0&&r.Left+r.Width<=window.ActualWidth+1.0&&r.Top+r.Height<=window.ActualHeight+1.0;}
 static int CheckLayout(bool requireAllCardsVisible){
  var scroll=(ScrollViewer)window.FindName("ContentScroll");
  if(scroll.ExtentWidth>scroll.ViewportWidth+1.0)return 1;
  foreach(string name in new[]{"Start","Uninstall","Scan","OpenApp","Cancel"}){
   var button=(Button)window.FindName(name);
   if(button.Visibility==Visibility.Visible&&(!Inside(button)||!button.IsHitTestVisible))return 2;
  }
  foreach(string name in new[]{"Progress","Status","SelectionInfo"})
   if(!Inside((FrameworkElement)window.FindName(name)))return 3;
  for(int i=0;i<Count;i++){
   var card=(FrameworkElement)window.FindName("Pick"+i);
   scroll.ScrollToTop();window.UpdateLayout();card.BringIntoView();window.UpdateLayout();
   if(!Inside(card))return 4;
  }
  var folderBox=(FrameworkElement)window.FindName("Folder");
  folderBox.BringIntoView();window.UpdateLayout();
  if(!Inside(folderBox))return 5;
  if(requireAllCardsVisible){
   scroll.ScrollToTop();window.UpdateLayout();
   for(int i=0;i<Count;i++)if(!Inside(picks[i]))return 6;
  }
  scroll.ScrollToTop();return 0;
 }
 static void TestPackages(){
  foreach(var p in packages){
   p.Available=true;p.Reason=null;
   if(string.IsNullOrEmpty(p.Hash))p.Hash="test-"+p.Key;
   if(p.Bytes<=0)p.Bytes=1;
   if(string.IsNullOrEmpty(p.Name))p.Name="fixture-"+p.Key+".zip";
   if(string.IsNullOrEmpty(p.Version))p.Version="0.0-test";
  }
 }

 [STAThread] static int Main(string[] args){
  AppContext.SetSwitch("Switch.System.IO.UseLegacyPathHandling",false);
  AppContext.SetSwitch("Switch.System.IO.BlockLongPaths",false);
  ServicePointManager.SecurityProtocol=SecurityProtocolType.Tls12;ServicePointManager.DefaultConnectionLimit=16;
  cache=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"MoleculeStudioInstaller","cache");
  Directory.CreateDirectory(cache);
  if(args.Length>0&&args[0]!="--preview"&&args[0]!="--ui-lifecycle-test"&&args[0]!="--small-screen-test"){try{
   if(args[0]=="--ocsr-config-test"){
    string root=Path.Combine(Path.GetTempPath(),"MoleculeStudio-ocsr-config-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(root);
    string rel="ocsr_runtime/venv/pyvenv.cfg",cfg=Safe(root,rel);Directory.CreateDirectory(Path.GetDirectoryName(cfg));
    string leftover="home = C:\\old-install\\python\ninclude-system-site-packages = false\nversion_info = 3.10\n";File.WriteAllText(cfg,leftover);
    string zip=Path.Combine(root,"fixture.zip");
    using(var z=ZipFile.Open(zip,ZipArchiveMode.Create))using(var w=new StreamWriter(z.CreateEntry("MoleculeStudio/"+rel).Open()))w.Write("home = ..\\python\\cpython-3.10-windows-x86_64-none\ninclude-system-site-packages = false\nversion_info = 3.10\n");
    // Reproduce a failed previous uninstall: leftover cfg, no OCSR receipt.
    BeginTransaction(root);Extract(zip,root,packages[3]);if(!ValidGeneratedConfig(root,packages[3],rel))return 80;RollbackTransaction();if(File.ReadAllText(cfg)!=leftover)return 81;
    BeginTransaction(root);Extract(zip,root,packages[3]);CommitTransaction();if(!Verify(root,packages[3]))return 82;
    // The application rewrites line endings/home; checksum changes but semantics do not.
    File.WriteAllText(cfg,RuntimeConfig(root).Replace("\n","\r\n"));if(!Verify(root,packages[3]))return 83;
    File.WriteAllText(Path.Combine(root,"user.cdxml"),"keep");Remove(root,packages[3]);if(File.Exists(cfg)||Has(root,packages[3])||!File.Exists(Path.Combine(root,"user.cdxml")))return 84;
    BeginTransaction(root);Extract(zip,root,packages[3]);CommitTransaction();return Verify(root,packages[3])?0:85;
   }
   if(args[0]=="--state-test"&&args.Length==3)return State(args[1],packages[0])==int.Parse(args[2])?0:65;
   if(args[0]=="--feed-test"&&(args.Length==2||args.Length==3)){
    var files=new List<string>{args[1]};if(args.Length==3)files.Add(args[2]);
    foreach(string file in files){
     string text=File.ReadAllText(file);
     var root=Json().Deserialize<Dictionary<string,object>>(text);
     int schema=Convert.ToInt32(root["schema"]);
     var parsed=ParseFeed(text);
     if(parsed.Length!=4)return 50;
     if(parsed[0].Key!="Core"||parsed[1].Key!="AIBasic"||parsed[2].Key!="AIAdvanced"||parsed[3].Key!="OCSR")return 51;
     if(schema==1){
      if(!parsed[1].Available||parsed[2].Available||parsed[1].Version!=parsed[0].Version)return 52;
      if(!Rejects(CloneFeed(root),r=>r.RemoveAll(x=>(string)x["id"]=="AI")))return 53;
      if(!Rejects(CloneFeed(root),r=>r[0]["url"]="https://evil.invalid/"+r[0]["name"]))return 54;
      if(!Rejects(CloneFeed(root),r=>r[1]["requires_core_version"]="9.9.9"))return 55;
      if(!RejectsProtocol(root))return 56;
     }else if(schema==2){
      if(!parsed[1].Available||!parsed[2].Available||!parsed[3].Available)return 57;
      if(Array.IndexOf(parsed[2].Requires,"AIBasic")<0)return 58;
      if(!Rejects(CloneFeed(root),r=>r.RemoveAll(x=>(string)x["id"]=="AIBasic")))return 59;
      if(!Rejects(CloneFeed(root),r=>r[0]["id"]="AITurbo"))return 60;
      if(!Rejects(CloneFeed(root),r=>r.Add(CloneRow(r[1]))))return 61;
      if(!Rejects(CloneFeed(root),r=>r[1]["url"]="https://evil.invalid/"+(string)r[1]["name"]))return 62;
      if(!Rejects(CloneFeed(root),r=>r[1]["requires"]=new object[]{"AITurbo"}))return 63;
      if(!Rejects(CloneFeed(root),r=>r[1]["requires_core_version"]="9.9.9"))return 64;
      if(!RejectsProtocol(root))return 66;
     }else return 67;
    }
    return 0;
   }
   if(args[0]=="--batch-test"){
    if(SelectionMode(new[]{true,true,true,true},new[]{1,0,0,0})!=0)return 67;
    if(SelectionMode(new[]{false,true,true,false},new[]{1,0,0,0})!=1)return 68;
    if(SelectionMode(new[]{true,false,false,false},new[]{1,0,0,0})!=2)return 69;
    if(SelectionMode(new[]{false,false,false,false},new[]{1,0,0,0})!=0)return 70;
    if(SelectionMode(new[]{true,false,false,false},new[]{4,0,0,0})!=1)return 71;
    if(string.Join(",",Plan(new[]{false,true,true,false},new[]{0,0,0,0},false))!="0,1,2")return 42;
    if(string.Join(",",Plan(new[]{false,true,true,false},new[]{1,0,0,0},false))!="1,2")return 43;
    if(string.Join(",",Plan(new[]{true,false,false,false},new[]{1,1,1,1},true))!="3,2,1,0")return 44;
    if(Plan(new[]{false,false,false,false},new[]{1,1,1,1},false).Length!=0)return 45;
    if(string.Join(",",Plan(new[]{true,false,false,false},new[]{4,4,4,4},false))!="0,1,2,3")return 46;
    if(string.Join(",",Plan(new[]{false,false,true,false},new[]{1,0,0,0},false))!="1,2")return 47;
    if(string.Join(",",Plan(new[]{false,true,false,false},new[]{1,1,1,0},true))!="2,1")return 48;
    if(string.Join(",",Plan(new[]{false,false,false,true},new[]{1,1,1,0},false))!="3")return 49;
    if(!CanInstall(0,0,0)||CanInstall(1,0,0)||CanInstall(2,1,0)||!CanInstall(2,1,1)||!CanInstall(3,1,0)||CanInstall(3,0,0))return 41;
    if(SelectionMode(new[]{false,true,false,false},new[]{1,2,0,0})!=1)return 75;
    if(SelectionMode(new[]{true,false,false,false},new[]{1,2,0,0})!=2)return 77;
    return 0;
   }
   if(args[0]=="--repair-test"){
    string root=Path.Combine(Path.GetTempPath(),"MoleculeStudio-repair-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(root);
    File.WriteAllText(Path.Combine(root,"MoleculeStudio.exe"),"old");File.WriteAllText(Path.Combine(root,"settings.ini"),"original");
    var receipt=new Dictionary<string,string>{{"MoleculeStudio.exe",Hash(Path.Combine(root,"MoleculeStudio.exe"))},{"settings.ini",Hash(Path.Combine(root,"settings.ini"))}};
    File.WriteAllText(Path.Combine(root,".installer-Core.json"),Json().Serialize(new{packageHash="test",files=receipt}));
    // A patched executable must be replaceable during an update.
    File.WriteAllText(Path.Combine(root,"MoleculeStudio.exe"),"patched");
    string zip=Path.Combine(root,"core.zip");
    using(var z=ZipFile.Open(zip,ZipArchiveMode.Create))using(var w=new StreamWriter(z.CreateEntry("MoleculeStudio/MoleculeStudio.exe").Open()))w.Write("new");
    var pkg=new Package{Key="Core",Name="core.zip",Hash="test",Version="test"};
    BeginTransaction(root);Extract(zip,root,pkg);CommitTransaction();
    if(File.ReadAllText(Path.Combine(root,"MoleculeStudio.exe"))!="new")return 71;
    // A modified settings.ini must still stop the install and roll back.
    File.WriteAllText(Path.Combine(root,"settings.ini"),"user changed");
    string zip2=Path.Combine(root,"core2.zip");
    using(var z=ZipFile.Open(zip2,ZipArchiveMode.Create)){using(var w=new StreamWriter(z.CreateEntry("MoleculeStudio/MoleculeStudio.exe").Open()))w.Write("new");using(var w=new StreamWriter(z.CreateEntry("MoleculeStudio/settings.ini").Open()))w.Write("new settings payload");}
    BeginTransaction(root);Extract(zip2,root,pkg);CommitTransaction();
    if(File.ReadAllText(Path.Combine(root,"settings.ini"))!="new settings payload")return 72;
    // A runtime-regenerated .pyc must not block the install.
    string pyc=Path.Combine(root,"Lib","__pycache__","base.cpython-310.pyc");Directory.CreateDirectory(Path.GetDirectoryName(pyc));File.WriteAllText(pyc,"old bytecode");
    var receipt2=new Dictionary<string,string>{{"MoleculeStudio.exe",Hash(Path.Combine(root,"MoleculeStudio.exe"))},{"Lib/__pycache__/base.cpython-310.pyc",Hash(pyc)}};
    File.WriteAllText(Path.Combine(root,".installer-Core.json"),Json().Serialize(new{packageHash="test",files=receipt2}));
    File.WriteAllText(pyc,"regenerated bytecode");
    string zip3=Path.Combine(root,"core3.zip");
    using(var z=ZipFile.Open(zip3,ZipArchiveMode.Create))using(var w=new StreamWriter(z.CreateEntry("MoleculeStudio/Lib/__pycache__/base.cpython-310.pyc").Open()))w.Write("package bytecode");
    BeginTransaction(root);Extract(zip3,root,pkg);CommitTransaction();
    if(File.ReadAllText(pyc)!="regenerated bytecode")return 73;
    return 0;
   }
   if(args[0]=="--rollback-test"){
    string root=Path.Combine(Path.GetTempPath(),"MoleculeStudio-rollback-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(root);
    File.WriteAllText(Path.Combine(root,"owned.txt"),"old");File.WriteAllText(Path.Combine(root,"user.txt"),"keep");
    var p=new Package{Key="AIBasic",Name="fixture.zip",Hash=packages[1].Hash,Version=packages[1].Version};
    File.WriteAllText(Path.Combine(root,".installer-AIBasic.json"),Json().Serialize(new{packageHash=p.Hash,files=new Dictionary<string,string>{{"owned.txt",Hash(Path.Combine(root,"owned.txt"))}}}));
    string zip=Path.Combine(root,"fixture.zip");
    using(var z=ZipFile.Open(zip,ZipArchiveMode.Create)){using(var w=new StreamWriter(z.CreateEntry("MoleculeStudio/owned.txt").Open()))w.Write("new-content");using(var w=new StreamWriter(z.CreateEntry("MoleculeStudio/added.txt").Open()))w.Write("added");}
    BeginTransaction(root);Extract(zip,root,p);undo=null;Recover(root);if(File.ReadAllText(Path.Combine(root,"owned.txt"))!="old"||File.Exists(Path.Combine(root,"added.txt")))return 47;
    BeginTransaction(root);Extract(zip,root,p);CommitTransaction();if(File.ReadAllText(Path.Combine(root,"owned.txt"))!="new-content"||File.ReadAllText(Path.Combine(root,"user.txt"))!="keep")return 48;
    return Verify(root,p)?0:49;
   }
   if(args[0]=="--shortcut-test"){
    string t=Path.Combine(Path.GetTempPath(),"MoleculeStudio-shortcut-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(t);
    string desktop=Path.Combine(t,"Desktop"),root=Path.Combine(t,"中文 安装目录");Directory.CreateDirectory(root);File.Copy(Assembly.GetExecutingAssembly().Location,Path.Combine(root,"MoleculeStudio.exe"));
    Shortcut(root,desktop,false);string link=ShortcutPath(root,desktop,false);if(!File.Exists(link))return 20;byte[] before=File.ReadAllBytes(link);Shortcut(root,desktop,false);if(!Convert.ToBase64String(before).Equals(Convert.ToBase64String(File.ReadAllBytes(link))))return 21;
    Shortcut(Path.Combine(t,"other"),desktop,true);if(!File.Exists(link))return 22;Shortcut(root,desktop,true);if(File.Exists(link))return 23;
    // Another installation owns the normal name: create a distinct shortcut, never overwrite it.
    string other=Path.Combine(t,"other");Directory.CreateDirectory(other);File.Copy(Assembly.GetExecutingAssembly().Location,Path.Combine(other,"MoleculeStudio.exe"));Shortcut(other,desktop,false);Shortcut(root,desktop,false);if(!File.Exists(ShortcutPath(root,desktop,true)))return 24;Shortcut(root,desktop,true);if(!File.Exists(link)||File.Exists(ShortcutPath(root,desktop,true)))return 25;Shortcut(other,desktop,true);return 0;
   }
   if(args[0]=="--dependency-test"){
    TestPackages();
    string t=Path.Combine(Path.GetTempPath(),"MoleculeStudio-dependency-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(t);
    if(State(t,packages[0])!=0||CanInstall(1,0,0)||CanInstall(2,1,0)||CanInstall(3,0,0)||!CanInstall(0,0,0))return 30;
    bool refused=false;try{RequireCore(t,1);}catch(IOException){refused=true;}if(!refused)return 31;
    refused=false;try{RequireCore(t,3);}catch(IOException){refused=true;}if(!refused)return 32;
    var record=new Dictionary<string,string>{{"core.bin","unused"},{"present.bin","unused"}};
    File.WriteAllText(Path.Combine(t,"present.bin"),"fixture");
    File.WriteAllText(Path.Combine(t,".installer-Core.json"),Json().Serialize(new{packageHash=packages[0].Hash,files=record}));
    if(State(t,packages[0])!=2)return 33;
    File.WriteAllText(Path.Combine(t,"core.bin"),"fixture");if(State(t,packages[0])!=1||!CanInstall(3,1,0))return 34;
    RequireCore(t,3);
    // AIAdvanced requires an installed AIBasic.
    refused=false;try{RequireCore(t,2);}catch(IOException){refused=true;}if(!refused)return 35;
    var basic=new Dictionary<string,string>{{"basic.bin","unused"}};File.WriteAllText(Path.Combine(t,"basic.bin"),"fixture");
    File.WriteAllText(Path.Combine(t,".installer-AIBasic.json"),Json().Serialize(new{packageHash=packages[1].Hash,files=basic}));
    if(State(t,packages[1])!=1||!CanInstall(2,1,1))return 36;
    RequireCore(t,2);return 0;
   }
   if(args[0]=="--lifecycle-test"){
    string t=Path.Combine(Path.GetTempPath(),"MoleculeStudio-lifecycle-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(t);
    File.WriteAllText(Path.Combine(t,"shared.dll"),"shared");File.WriteAllText(Path.Combine(t,"basic.bin"),"basic");File.WriteAllText(Path.Combine(t,"plugin.bin"),"plugin");File.WriteAllText(Path.Combine(t,"ocsr.bin"),"ocsr");File.WriteAllText(Path.Combine(t,"settings.ini"),"original");File.WriteAllText(Path.Combine(t,"user.cdxml"),"drawing");
    var core=new Dictionary<string,string>{{"shared.dll",Hash(Path.Combine(t,"shared.dll"))}};
    var basic=new Dictionary<string,string>{{"shared.dll",Hash(Path.Combine(t,"shared.dll"))},{"basic.bin",Hash(Path.Combine(t,"basic.bin"))},{"settings.ini",Hash(Path.Combine(t,"settings.ini"))}};
    var advanced=new Dictionary<string,string>{{"shared.dll",Hash(Path.Combine(t,"shared.dll"))},{"plugin.bin",Hash(Path.Combine(t,"plugin.bin"))}};
    var ocsr=new Dictionary<string,string>{{"shared.dll",Hash(Path.Combine(t,"shared.dll"))},{"ocsr.bin",Hash(Path.Combine(t,"ocsr.bin"))}};
    File.WriteAllText(Path.Combine(t,".installer-Core.json"),Json().Serialize(new{packageHash=packages[0].Hash,files=core}));
    File.WriteAllText(Path.Combine(t,".installer-AIBasic.json"),Json().Serialize(new{packageHash=packages[1].Hash,files=basic}));
    File.WriteAllText(Path.Combine(t,".installer-AIAdvanced.json"),Json().Serialize(new{packageHash=packages[2].Hash,files=advanced}));
    File.WriteAllText(Path.Combine(t,".installer-OCSR.json"),Json().Serialize(new{packageHash=packages[3].Hash,files=ocsr}));
    File.WriteAllText(Path.Combine(t,"settings.ini"),"user changed");
    bool coreProtected=false;try{Remove(t,packages[0]);}catch(IOException){coreProtected=true;}if(!coreProtected||!File.Exists(Path.Combine(t,"shared.dll")))return 15;
    // Removing AIBasic keeps files shared with the other three components and keeps
    // user-modified files; it drops only its own files and receipt.
    if(Remove(t,packages[1])!=1||!File.Exists(Path.Combine(t,"shared.dll"))||File.Exists(Path.Combine(t,"basic.bin"))||!File.Exists(Path.Combine(t,"plugin.bin"))||Has(t,packages[1]))return 10;
    if(!Verify(t,packages[0]))return 11;
    Remove(t,packages[2]);if(File.Exists(Path.Combine(t,"plugin.bin"))||!File.Exists(Path.Combine(t,"shared.dll")))return 16;
    Remove(t,packages[3]);if(File.Exists(Path.Combine(t,"ocsr.bin"))||!File.Exists(Path.Combine(t,"shared.dll")))return 17;
    Remove(t,packages[0]);if(File.Exists(Path.Combine(t,"shared.dll"))||!File.Exists(Path.Combine(t,"user.cdxml"))||!File.Exists(Path.Combine(t,"settings.ini")))return 12;
    // Adding the component again must recreate only its owned files.
    string zip=Path.Combine(t,"addon.zip");using(var z=ZipFile.Open(zip,ZipArchiveMode.Create))using(var w=new StreamWriter(z.CreateEntry("MoleculeStudio/basic.bin").Open()))w.Write("basic");
    Extract(zip,t,packages[1]);if(!Verify(t,packages[1]))return 13;Remove(t,packages[1]);if(!File.Exists(Path.Combine(t,"user.cdxml")))return 14;
    // Legacy three-part AI receipt is read as AIBasic and upgraded in place.
    string legacy=Path.Combine(t,"legacy");Directory.CreateDirectory(legacy);Directory.CreateDirectory(Path.Combine(legacy,"ai_plugins"));
    File.WriteAllText(Path.Combine(legacy,"ai_lib.bin"),"ai");File.WriteAllText(Path.Combine(legacy,"ai_plugins/plugin.bin"),"plugin");
    var legacyFiles=new Dictionary<string,object>{{"ai_lib.bin",new Dictionary<string,object>{{"bytes",2},{"sha256",Hash(Path.Combine(legacy,"ai_lib.bin"))}}},{"ai_plugins/plugin.bin",new Dictionary<string,object>{{"bytes",6},{"sha256",Hash(Path.Combine(legacy,"ai_plugins/plugin.bin"))}}}};
    File.WriteAllText(Path.Combine(legacy,"MANIFEST-AI.json"),Json().Serialize(new{version=packages[1].Version,package="AI",files=legacyFiles}));
    if(!Has(legacy,packages[1])||State(legacy,packages[1])!=1)return 18;
    string split=Path.Combine(t,"split.zip");using(var z=ZipFile.Open(split,ZipArchiveMode.Create))using(var w=new StreamWriter(z.CreateEntry("MoleculeStudio/ai_lib.bin").Open()))w.Write("ai");
    BeginTransaction(legacy);Extract(split,legacy,packages[1]);CommitTransaction();
    if(File.Exists(Path.Combine(legacy,"ai_plugins/plugin.bin"))||File.Exists(Path.Combine(legacy,"MANIFEST-AI.json"))||!Verify(legacy,packages[1]))return 19;
    return 0;
   }
   if(args[0]=="--self-test"){
    bool rejected=false;try{Safe(cache,"../../escape");}catch(IOException){rejected=true;}if(!rejected)return 2;
    string t=Path.Combine(Path.GetTempPath(),"MoleculeStudio-installer-test-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(t);
    string z=Path.Combine(t,"fixture.zip"),dest=Path.Combine(t,"installed");Directory.CreateDirectory(dest);
    using(var archive=ZipFile.Open(z,ZipArchiveMode.Create)){foreach(string rel in new[]{"chemistry_python/Lib/site-packages/example.txt","ocsr_runtime/venv/Lib/site-packages/example.txt"}){using(var w=new StreamWriter(archive.CreateEntry("MoleculeStudio/"+rel).Open()))w.Write("same immutable content");}}
    var fixture=new Package{Key="Test",Name="fixture.zip",Hash="fixture",Version="test"};
    Extract(z,dest,fixture);if(!Verify(dest,fixture)||saved==0)return 3;Extract(z,dest,fixture);
    File.WriteAllText(Path.Combine(dest,"chemistry_python/Lib/site-packages/example.txt"),"modified content that changes the size");if(Verify(dest,fixture))return 4;
    Extract(z,dest,fixture);if(!Verify(dest,fixture))return 5;
    return 0;
   }
   if(args[0]=="--extract-test"&&args.Length==4){
    int i=int.Parse(args[1]);if(i<0||i>=Count)return 3;
    if(Hash(args[2])!=packages[i].Hash)throw new IOException("hash mismatch");
    Extract(args[2],args[3],packages[i]);return Verify(args[3],packages[i])?0:3;
   }
   if(args[0]=="--health-test"&&(args.Length==2||args.Length==3)){int index=args.Length==3?int.Parse(args[2]):0;if(index<0||index>=Count)return 3;Health(args[1],packages[index]);return 0;}
   if(args[0]=="--parallel-test"){
    string root=Path.Combine(Path.GetTempPath(),"MoleculeStudio-parallel-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(root);
    string zips=Path.Combine(root,"_zips");Directory.CreateDirectory(zips);
    var originalPackages=packages;string originalCache=cache;
    var fixtures=new Package[Count];
    for(int i=0;i<Count;i++){
     var fixture=new Package{Key=Keys[i],Name=Keys[i]+"-fixture.zip",Hash="",Bytes=1,Available=true,Version="0.0-test",Display=Titles[i]};
     string file=Path.Combine(zips,fixture.Name);
     using(var z=ZipFile.Open(file,ZipArchiveMode.Create)){
      for(int j=0;j<40;j++)using(var w=new StreamWriter(z.CreateEntry("MoleculeStudio/"+Keys[i]+"-"+j+".txt").Open()))w.Write(Keys[i]+" entry "+j);
      using(var w=new StreamWriter(z.CreateEntry("MoleculeStudio/shared-"+(i%2)+".txt").Open()))w.Write("shared "+(i%2));
     }
     fixture.Hash=Hash(file);fixtures[i]=fixture;
    }
    string install=Path.Combine(root,"install");Directory.CreateDirectory(install);
    packages=fixtures;testSource=zips;cache=Path.Combine(root,"_cache");Directory.CreateDirectory(cache);
    try{
     InstallAll(install,new[]{0,1,2,3},false,false,false);
     for(int i=0;i<Count;i++)if(!Has(install,fixtures[i])||State(install,fixtures[i])!=1)return 90+i;
     if(!File.Exists(Path.Combine(install,"Core-0.txt"))||!File.Exists(Path.Combine(install,"OCSR-39.txt")))return 94;
    }finally{packages=originalPackages;cache=originalCache;testSource=null;}
    return 0;
   }
   if(args[0]=="--download-test"&&args.Length==4){
    var download=new Package{Key="Download",Name="download-test-"+Guid.NewGuid().ToString("N")+".zip",Hash=args[2],Bytes=long.Parse(args[3]),Available=true,Url=args[1],Display="下载测试"};
    string path=Download(download,false,-1);
    bool ok=Hash(path)==args[2];
    if(File.Exists(path))File.Delete(path);
    return ok?0:1;
   }
   if(args[0]=="--network-test"){Download(new Package{Key="Network test",Name="MoleculeStudio-0.95-external-ai-skill.zip",Hash="1ce9a37f57069234d0e5c892c74e32cde8963bbb21d769155e69e11e88674a80",Bytes=1,Available=true,Url=ReleaseBase+"v0.9.5.0.0/MoleculeStudio-0.95-external-ai-skill.zip"},true);return 0;}
  }catch(Exception error){Console.Error.WriteLine(error);return 1;}return 2;}

  var app=new Application();
  using(var stream=Assembly.GetExecutingAssembly().GetManifestResourceStream("Installer.xaml"))window=(Window)XamlReader.Load(stream);
  folder=(TextBox)window.FindName("Folder");status=(TextBlock)window.FindName("Status");bar=(ProgressBar)window.FindName("Progress");
  start=(Button)window.FindName("Start");scan=(Button)window.FindName("Scan");mirror=(CheckBox)window.FindName("Mirrors");uninstall=(Button)window.FindName("Uninstall");
  desktopShortcut=(CheckBox)window.FindName("DesktopShortcut");
  using(var icon=Assembly.GetExecutingAssembly().GetManifestResourceStream("App.ico")){window.Icon=System.Windows.Media.Imaging.BitmapFrame.Create(icon,System.Windows.Media.Imaging.BitmapCreateOptions.None,System.Windows.Media.Imaging.BitmapCacheOption.OnLoad);}
  folder.Text=File.Exists(LastRoot)?File.ReadAllText(LastRoot):Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"Programs","MoleculeStudio");
  picks=new CheckBox[Count];badges=new TextBlock[Count];
  jobRows=new Grid[Count];jobBars=new ProgressBar[Count];jobInfos=new TextBlock[Count];
  for(int i=0;i<Count;i++){jobRows[i]=(Grid)window.FindName("Job"+i);jobBars[i]=(ProgressBar)window.FindName("Bar"+i);jobInfos[i]=(TextBlock)window.FindName("Job"+i+"Info");}
  for(int i=0;i<Count;i++){picks[i]=(CheckBox)window.FindName("Pick"+i);badges[i]=(TextBlock)window.FindName("State"+i);picks[i].Checked+=(sender,e)=>Buttons();picks[i].Unchecked+=(sender,e)=>Buttons();}
  bool syncing=false;
  // Selecting AIAdvanced implies AIBasic; dropping AIBasic drops AIAdvanced.
  picks[2].Checked+=(sender,e)=>{if(syncing)return;syncing=true;if(picks[1].IsEnabled)picks[1].IsChecked=true;syncing=false;Buttons();};
  picks[1].Unchecked+=(sender,e)=>{if(syncing)return;syncing=true;picks[2].IsChecked=false;syncing=false;Buttons();};
  start.Click+=(sender,e)=>Batch(false);uninstall.Click+=(sender,e)=>Batch(true);
  ((Button)window.FindName("OpenApp")).Click+=(sender,e)=>{try{if(!CanOpen(folder.Text)){Cards(folder.Text);Report("核心程序不存在或安装不完整，请勾选核心重新安装。",0);return;}Process.Start(new ProcessStartInfo(Path.Combine(folder.Text,"MoleculeStudio.exe")){WorkingDirectory=folder.Text,UseShellExecute=true});}catch(Exception error){Report(error.Message,0);}};
  scan.Click+=(s,e)=>{string root=folder.Text;Work(()=>{var results=new List<string>();foreach(var p in packages){bool valid=false;try{valid=Verify(root,p);}catch(OperationCanceledException){throw;}catch{}results.Add(p.Display+"："+(valid?"已安装且完整":"未安装或需要校验安装"));}Report(string.Join("    ",results.ToArray()),100);});};
  ((Button)window.FindName("Cancel")).Click+=(s,e)=>{cancelled=true;Report("正在停止，请等待当前网络请求结束……",-1);};
  ((Button)window.FindName("Browse")).Click+=(s,e)=>{using(var dialog=new System.Windows.Forms.FolderBrowserDialog()){if(dialog.ShowDialog()==System.Windows.Forms.DialogResult.OK)folder.Text=dialog.SelectedPath;}};
  window.Closing+=(s,e)=>{if(busy){cancelled=true;e.Cancel=true;Report("正在停止，请稍后关闭窗口……",-1);}};
  FitWindow();window.SizeChanged+=(s,e)=>LayoutForSize();

  if(args.Length>0&&args[0]=="--ui-lifecycle-test"){
   TestPackages();
   string t=Path.Combine(Path.GetTempPath(),"MoleculeStudio-ui-lifecycle-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(t);
   string root=Path.Combine(t,"installed"),zip=Path.Combine(t,"core.zip");folder.Text=root;
   using(var z=ZipFile.Open(zip,ZipArchiveMode.Create)){
    using(var input=File.OpenRead(Assembly.GetExecutingAssembly().Location))using(var output=z.CreateEntry("MoleculeStudio/MoleculeStudio.exe").Open())input.CopyTo(output);
    using(var w=new StreamWriter(z.CreateEntry("MoleculeStudio/MANIFEST-Core.json").Open()))w.Write("{}");
   }
   Extract(zip,root,packages[0]);Cards(root);picks[0].IsChecked=true;EnableActions(true);
   if(states[0]!=1||!uninstall.IsEnabled||start.IsEnabled||!((Button)window.FindName("OpenApp")).IsEnabled)return 60;
   // AIAdvanced selection automatically selects AIBasic and keeps install order.
   picks[0].IsChecked=false;picks[2].IsChecked=true;Buttons();
   if(picks[1].IsChecked!=true)return 72;
   // basic missing/current? core is current so plan is [AIBasic, AIAdvanced]
   if(!start.IsEnabled||uninstall.IsEnabled)return 73;
   string order=((TextBlock)window.FindName("SelectionInfo")).Text;
   if(order.IndexOf("AI 基础",StringComparison.Ordinal)<0||order.IndexOf("AI 高级",StringComparison.Ordinal)<0)return 74;
   picks[1].IsChecked=false;if(picks[2].IsChecked!=false)return 75;
   // Install fake AIBasic/AIAdvanced receipts so uninstall cascades can be checked.
   File.WriteAllText(Path.Combine(root,"basic.bin"),"basic");File.WriteAllText(Path.Combine(root,"plugin.bin"),"plugin");
   File.WriteAllText(Path.Combine(root,".installer-AIBasic.json"),Json().Serialize(new{packageHash=packages[1].Hash,files=new Dictionary<string,string>{{"basic.bin",Hash(Path.Combine(root,"basic.bin"))}}}));
   File.WriteAllText(Path.Combine(root,".installer-AIAdvanced.json"),Json().Serialize(new{packageHash=packages[2].Hash,files=new Dictionary<string,string>{{"plugin.bin",Hash(Path.Combine(root,"plugin.bin"))}}}));
   Cards(root);EnableActions(true);
   if(states[1]!=1||states[2]!=1)return 76;
   picks[1].IsChecked=true;Buttons();
   if(!uninstall.IsEnabled||uninstall.Content as string!="卸载所选 (2)")return 77;
   picks[1].IsChecked=false;picks[0].IsChecked=true;Buttons();
   if(!uninstall.IsEnabled||uninstall.Content as string!="卸载所选 (3)")return 78;
   // Mixed install/uninstall selection is refused.
   picks[3].IsChecked=true;Buttons();if(start.IsEnabled||uninstall.IsEnabled)return 79;
   picks[0].IsChecked=false;picks[2].IsChecked=false;picks[3].IsChecked=false;
   // Remove everything through the normal path; external stale receipt must not
   // leave a working Open button.
   Remove(root,packages[2]);Remove(root,packages[1]);Remove(root,packages[0]);
   picks[0].IsChecked=true;Cards(root);EnableActions(true);
   if(states[0]!=0||uninstall.IsEnabled||!start.IsEnabled||((Button)window.FindName("OpenApp")).Visibility!=Visibility.Collapsed)return 61;
   states=new[]{1,1,1,1};Cards(root);EnableActions(true);if(states[0]!=0||CanOpen(root)||uninstall.Visibility!=Visibility.Collapsed)return 62;
   using(var child=Process.Start(new ProcessStartInfo(Assembly.GetExecutingAssembly().Location,"--state-test \""+root+"\" 0"){UseShellExecute=false,CreateNoWindow=true})){if(!child.WaitForExit(10000)||child.ExitCode!=0)return 66;}
   Extract(zip,root,packages[0]);Cards(root);EnableActions(true);if(states[0]!=1||!CanOpen(root))return 63;
   File.Delete(Path.Combine(root,"MoleculeStudio.exe"));Cards(root);EnableActions(true);
   if(states[0]!=0||!start.IsEnabled||((Button)window.FindName("OpenApp")).Visibility!=Visibility.Collapsed)return 64;
   window.Close();return 0;
  }
  if(args.Length>0&&args[0]=="--small-screen-test"){
   TestPackages();
   var cases=new object[][]{
    new object[]{"1024x600",1024.0,600.0,true},
    new object[]{"1366x768",1366.0,768.0,true},
    new object[]{"1024x600@125%",819.0,480.0,false},
    new object[]{"1366x768@125%",1093.0,614.0,false},
    new object[]{"1024x600@150%",682.0,400.0,false},
    new object[]{"1366x768@150%",911.0,512.0,false}};
   window.Show();
   foreach(var item in cases){
    window.Width=(double)item[1];window.Height=(double)item[2];window.UpdateLayout();LayoutForSize();window.UpdateLayout();
    picks[0].IsChecked=true;Buttons();window.UpdateLayout();
    int code=CheckLayout((bool)item[3]);
    if(!start.IsEnabled&&code==0)code=7;
    if(code!=0){Console.Error.WriteLine("small screen failed: "+item[0]+" code "+code);window.Close();return 90+code;}
    picks[0].IsChecked=false;Buttons();
   }
   window.Close();return 0;
  }
  if(args.Length>=2&&args[0]=="--preview"){
   // Preview renders the four available components with test data so the layout can
   // be reviewed without a live feed; it does not change detection behaviour.
   TestPackages();
   for(int i=2;i<args.Length;i++){
    var match=Regex.Match(args[i],"^(\\d{3,4})x(\\d{3,4})$");
    if(match.Success){window.Width=double.Parse(match.Groups[1].Value);window.Height=double.Parse(match.Groups[2].Value);}
    else if(args[i]=="select"){picks[1].IsChecked=true;picks[2].IsChecked=true;picks[3].IsChecked=true;}
    else if(!args[i].StartsWith("--"))folder.Text=args[i];
   }
   Cards(folder.Text);EnableActions(true);LayoutForSize();status.Text=feedStatus;
   if(Array.IndexOf(args,"--jobs")>=0){ShowJobs(new[]{0,1,2,3});jobBars[0].Value=76;jobInfos[0].Text="下载 核心工作台 · 812 MB / 1063 MB · 6.4 MB/s · 4 线程";jobBars[1].Value=100;jobInfos[1].Text="下载 AI 基础 · 75 KB · 4 线程";jobBars[2].IsIndeterminate=true;jobInfos[2].Text="连接 ghfast.top · AI 高级";jobBars[3].Value=100;jobInfos[3].Foreground=DoneB;jobInfos[3].Text="已安装 · 完成";status.Text="正在同时下载并安装 4 个组件……";((TextBlock)window.FindName("SelectionInfo")).Text="将同时下载并安装："+Names(new[]{0,1,2,3})+" · 合计约 1.81 GB";}
   window.Show();window.UpdateLayout();
   var bitmap=new System.Windows.Media.Imaging.RenderTargetBitmap((int)window.ActualWidth,(int)window.ActualHeight,96,96,System.Windows.Media.PixelFormats.Pbgra32);
   bitmap.Render(window);
   var encoder=new System.Windows.Media.Imaging.PngBitmapEncoder();encoder.Frames.Add(System.Windows.Media.Imaging.BitmapFrame.Create(bitmap));
   using(var f=File.Create(args[1]))encoder.Save(f);
   window.Close();return 0;
  }
  var detectionTimer=new System.Windows.Threading.DispatcherTimer{Interval=TimeSpan.FromMilliseconds(700)};
  detectionTimer.Tick+=(s,e)=>{if(busy)return;detectionTimer.Stop();Detect();};
  folder.TextChanged+=(s,e)=>{detectionTimer.Stop();EnableActions(false);detectionTimer.Start();};
  window.ContentRendered+=(s,e)=>Detect();
  EnableActions(false);app.Run(window);return 0;
 }
 // Feed test helpers: clone the parsed manifest, apply one defect, expect a rejection.
 static Dictionary<string,object> CloneFeed(Dictionary<string,object> root){return Json().Deserialize<Dictionary<string,object>>(Json().Serialize(root));}
 static Dictionary<string,object> CloneRow(Dictionary<string,object> row){return Json().Deserialize<Dictionary<string,object>>(Json().Serialize(row));}
 static bool Rejects(Dictionary<string,object> root,Action<List<Dictionary<string,object>>> mutate){
  var clone=Json().Deserialize<Dictionary<string,object>>(Json().Serialize(root));
  var rows=new List<Dictionary<string,object>>();foreach(Dictionary<string,object> row in (IEnumerable)clone["components"])rows.Add(row);
  mutate(rows);clone["components"]=rows;
  try{ParseFeed(Json().Serialize(clone));return false;}catch(IOException){return true;}
 }
 static bool RejectsProtocol(Dictionary<string,object> root){
  var clone=CloneFeed(root);clone["min_installer_protocol"]=99;
  try{ParseFeed(Json().Serialize(clone));return false;}catch(IOException){return true;}
 }
}
