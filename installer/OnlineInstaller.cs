using System;
using System.IO;
using System.IO.Compression;
using System.Net;
using System.Security.Cryptography;
using System.Threading;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Markup;
using System.Reflection;
using System.Diagnostics;
using System.Collections.Generic;
using System.Web.Script.Serialization;
using System.Runtime.InteropServices;

class OnlineInstaller {
 const string Release="https://github.com/chaobada10086/MoleculeStudio/releases/download/v0.9.5.0.0/";
 class Package { public string Key,Name,Hash,Url,Version; public long Bytes; public Package(string k,string n,string h){Key=k;Name=n;Hash=h;Url=Release+n;Version=k=="Core"?"0.95-issue2-hotfix":k=="OCSR"?"0.95-hotfix2":"0.95";} }
 static Package[] packages={
 new Package("Core","MoleculeStudio-0.95-issue2-hotfix-Core-win64.zip","28b1da887bef991b54dab92c64f006658f97780cd7b41bd0c940ee5b3a952396"),
 new Package("AI","MoleculeStudio-0.95-AI-addon-win64.zip","aeb4c91db43defe9a956dcc518515f35000d0fb5a2fec4840c3c2fda1518e753"),
 new Package("OCSR","MoleculeStudio-0.95-OCSR-addon-win64-hotfix2.zip","e58933bf435a8f57b3e6e6a4c7f5f4188c04196250b7af72e66e215c6849f6ff")};
 static Window window; static TextBox folder; static TextBlock status; static ProgressBar bar; static Button start,scan; static CheckBox mirror; static bool busy; static string cache; static long saved; static bool cancelled;
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool CreateHardLink(string name,string existing,IntPtr reserved);
 static JavaScriptSerializer Json(){return new JavaScriptSerializer{MaxJsonLength=int.MaxValue};}
 static string feedStatus="内置发布版本（尚未联网确认）"; static bool feedChecked;
 static Package[] ParseFeed(string text){
 var d=Json().Deserialize<Dictionary<string,object>>(text);if(Convert.ToInt32(d["schema"])!=1||Convert.ToInt32(d["min_installer_protocol"])>1||(string)d["channel"]!="stable")throw new IOException("版本清单需要更新安装器");
 var rows=(System.Collections.IEnumerable)d["components"];var found=new Dictionary<string,Package>();var requires=new Dictionary<string,string>();
 foreach(Dictionary<string,object> row in rows){string key=(string)row["id"],name=(string)row["name"],hash=(string)row["sha256"],url=(string)row["url"],version=(string)row["version"];
 if((key!="Core"&&key!="AI"&&key!="OCSR")||found.ContainsKey(key)||string.IsNullOrWhiteSpace(version)||name!=Path.GetFileName(name)||name.IndexOfAny(Path.GetInvalidFileNameChars())>=0||!name.EndsWith(".zip")||!System.Text.RegularExpressions.Regex.IsMatch(hash,"^[0-9a-f]{64}$")||!url.StartsWith("https://github.com/chaobada10086/MoleculeStudio/releases/download/",StringComparison.Ordinal)||!url.EndsWith("/"+name,StringComparison.Ordinal))throw new IOException("版本清单内容无效");
 var p=new Package(key,name,hash){Url=url,Version=version,Bytes=Convert.ToInt64(row["bytes"])};if(p.Bytes<=0)throw new IOException("包大小无效");found.Add(key,p);requires[key]=row["requires_core_version"] as string;
 }if(found.Count!=3||requires["AI"]!=found["Core"].Version||requires["OCSR"]!=found["Core"].Version)throw new IOException("模组与核心版本不兼容");return new[]{found["Core"],found["AI"],found["OCSR"]};
 }
 static void LoadFeed(){if(feedChecked)return;feedChecked=true;
 // Version metadata comes only from the official HTTPS origin. Package mirrors
 // cannot substitute their own checksum manifest.
 foreach(string feedUrl in new[]{"https://raw.githubusercontent.com/chaobada10086/MoleculeStudio/main/updates/stable.json","https://api.github.com/repos/chaobada10086/MoleculeStudio/contents/updates/stable.json?ref=main"})try{Report("正在获取在线发布信息……",-1);var req=(HttpWebRequest)WebRequest.Create(feedUrl);req.UserAgent="MoleculeStudio-Installer/1.0";req.Accept="application/vnd.github.raw+json";req.Timeout=8000;req.ReadWriteTimeout=8000;req.CachePolicy=new System.Net.Cache.RequestCachePolicy(System.Net.Cache.RequestCacheLevel.NoCacheNoStore);
 using(var response=req.GetResponse())using(var input=response.GetResponseStream())using(var bytes=new MemoryStream()){byte[] buffer=new byte[8192];int n;while((n=input.Read(buffer,0,buffer.Length))>0){if(bytes.Length+n>1048576)throw new IOException("版本清单过大");bytes.Write(buffer,0,n);}packages=ParseFeed(System.Text.Encoding.UTF8.GetString(bytes.ToArray()));}
 feedStatus="在线版本 · 核心 "+packages[0].Version;return;
 }catch{}feedStatus="在线清单暂不可用 · 使用内置版本，未确认最新";
 }
 static bool Current(string root,Package p){string receipt=Path.Combine(root,".installer-"+p.Key+".json");if(File.Exists(receipt)){var d=Json().Deserialize<Dictionary<string,object>>(File.ReadAllText(receipt));return (string)d["packageHash"]==p.Hash;}
 // Preserve recognition of the original, published packages until they change.
 return p.Key=="Core"?p.Hash=="28b1da887bef991b54dab92c64f006658f97780cd7b41bd0c940ee5b3a952396":p.Key=="AI"?p.Hash=="aeb4c91db43defe9a956dcc518515f35000d0fb5a2fec4840c3c2fda1518e753":p.Hash=="e58933bf435a8f57b3e6e6a4c7f5f4188c04196250b7af72e66e215c6849f6ff";}
 static CheckBox desktopShortcut;
 const string ShortcutDescription="MoleculeStudio · 由安装器创建";
 static object ComGet(object obj,string name){return obj.GetType().InvokeMember(name,BindingFlags.GetProperty,null,obj,null);}
 static void ComSet(object obj,string name,object value){obj.GetType().InvokeMember(name,BindingFlags.SetProperty,null,obj,new object[]{value});}
 static string ShortcutPath(string root,string desktop,bool alternate){
 string suffix="";if(alternate){using(var bytes=new MemoryStream(System.Text.Encoding.UTF8.GetBytes(Path.GetFullPath(root).ToUpperInvariant())))suffix=" ("+Hash(bytes).Substring(0,8)+")";}
 return Path.Combine(desktop,"MoleculeStudio"+suffix+".lnk");
 }
 static void Shortcut(string root,string desktop,bool remove){
 root=Path.GetFullPath(root);string target=Path.Combine(root,"MoleculeStudio.exe");if(!remove&&!File.Exists(target))throw new IOException("创建快捷方式失败：未找到 MoleculeStudio.exe");
 Directory.CreateDirectory(desktop);object shell=Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell"));
 try{for(int i=0;i<2;i++){string path=ShortcutPath(root,desktop,i!=0);bool exists=File.Exists(path);if(remove&&!exists)continue;
 object link=shell.GetType().InvokeMember("CreateShortcut",BindingFlags.InvokeMethod,null,shell,new object[]{path});
 try{bool same=string.Equals((string)ComGet(link,"TargetPath"),target,StringComparison.OrdinalIgnoreCase);
 if(remove){if(same&&(string)ComGet(link,"Description")==ShortcutDescription)File.Delete(path);continue;}
 if(exists){if(same)return;if(i==0)continue;throw new IOException("桌面存在同名快捷方式，已保留，请手动处理："+path);}
 ComSet(link,"TargetPath",target);ComSet(link,"WorkingDirectory",root);ComSet(link,"IconLocation",target+",0");ComSet(link,"Description",ShortcutDescription);link.GetType().InvokeMember("Save",BindingFlags.InvokeMethod,null,link,null);return;
 }finally{Marshal.FinalReleaseComObject(link);}}
 }finally{Marshal.FinalReleaseComObject(shell);}
 }
 static Button uninstall; static bool managed; static string LastRoot {get{return Path.Combine(Path.GetDirectoryName(cache),"installation.txt");}}
 static bool Has(string root,Package p){return File.Exists(Path.Combine(root,".installer-"+p.Key+".json"))||File.Exists(Path.Combine(root,"MANIFEST-"+p.Key+".json"));}
 static Dictionary<string,string> Inventory(string root,Package p){
 string file=Path.Combine(root,".installer-"+p.Key+".json");bool legacy=!File.Exists(file);if(legacy)file=Path.Combine(root,"MANIFEST-"+p.Key+".json");
 var result=new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase);if(!File.Exists(file))return result;
 var data=Json().Deserialize<Dictionary<string,object>>(File.ReadAllText(file));
 foreach(var item in (Dictionary<string,object>)data["files"]){Safe(root,item.Key);result[item.Key]=legacy?(string)((Dictionary<string,object>)item.Value)["sha256"]:(string)item.Value;}
 return result;
 }
 static void NoLinks(string root,string file){
 string stop=Path.GetFullPath(root).TrimEnd('\\');string p=Path.GetDirectoryName(file);
 while(p!=null){if(Directory.Exists(p)&&(File.GetAttributes(p)&FileAttributes.ReparsePoint)!=0)throw new IOException("安装目录包含目录链接，已停止移除："+p);if(string.Equals(p.TrimEnd('\\'),stop,StringComparison.OrdinalIgnoreCase))break;p=Path.GetDirectoryName(p);}
 }
 static void EnsureClosed(string root){
 string prefix=Path.GetFullPath(root).TrimEnd('\\')+"\\";
 foreach(var process in Process.GetProcesses())using(process){string exe=null;try{exe=process.MainModule.FileName;}catch{}if(exe!=null&&exe.StartsWith(prefix,StringComparison.OrdinalIgnoreCase))throw new IOException("请先关闭此安装目录中的程序，再更改组件或卸载。");}
 }
 static int Remove(string root,Package package){
 if(package.Key=="Core"&&(Has(root,packages[1])||Has(root,packages[2])))throw new IOException("请先卸载 AI 助手和图片识别，再卸载核心工作台。");
 root=Path.GetFullPath(root);EnsureClosed(root);var files=Inventory(root,package);var shared=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
 foreach(var other in packages)if(other.Key!=package.Key)foreach(var f in Inventory(root,other).Keys)shared.Add(f);
 // Preflight all paths before deleting anything. Never recursively delete an installation tree.
 foreach(var rel in files.Keys)NoLinks(root,Safe(root,rel));
 int preserved=0,n=0;var dirs=new HashSet<string>();
 foreach(var item in files){CheckCancel();if(shared.Contains(item.Key))continue;string p=Safe(root,item.Key);
 if(File.Exists(p)){if(Hash(p)==item.Value){File.Delete(p);dirs.Add(Path.GetDirectoryName(p));}else preserved++;}
 if(++n%200==0)Report("移除 "+package.Key+" · "+n+" / "+files.Count,100.0*n/files.Count);
 }
 foreach(string prefix in new[]{".installer-","MANIFEST-"}){string metadata=Safe(root,prefix+package.Key+".json");if(File.Exists(metadata))File.Delete(metadata);}
 // Only remove empty directories, and only ancestors of files owned by this component.
 foreach(string dir in dirs){string d=dir;while(d!=null&&!string.Equals(d.TrimEnd('\\'),root.TrimEnd('\\'),StringComparison.OrdinalIgnoreCase)&&d.StartsWith(root+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase)){
 if(!Directory.Exists(d)||Directory.GetFileSystemEntries(d).Length!=0)break;Directory.Delete(d);d=Path.GetDirectoryName(d);}}
 return preserved;
 }
 static string[] Titles={"核心工作台","AI 助手","图片识别"};
 static int[] states={0,0,0}; static CheckBox[] picks; static TextBlock[] badges;
 static int State(string root,Package p){if(!Has(root,p))return 0;try{var files=Inventory(root,p);if(files.Count==0)return 2;foreach(var f in files.Keys)if(!File.Exists(Safe(root,f)))return 2;return Current(root,p)?1:4;}catch{return 3;}}
 static bool CanInstall(int index,int core){return index==0||core==1;}
 static void RequireCore(string root,int index){if(!CanInstall(index,State(root,packages[0])))throw new IOException("请先完整安装当前版本的核心工作台。");}
 static int[] Plan(bool[] selected,int[] actual,bool remove){var planned=new bool[3];for(int i=0;i<3;i++)planned[i]=selected[i]&&(remove?actual[i]!=0:actual[i]!=1);
 if(remove&&planned[0]){planned[1]=actual[1]!=0;planned[2]=actual[2]!=0;}
 if(!remove&&(planned[1]||planned[2])&&actual[0]!=1)planned[0]=true;
 if(!remove&&planned[0])for(int i=1;i<3;i++)if(actual[i]==4)planned[i]=true;
 var list=new List<int>();for(int j=0;j<3;j++){int i=remove?2-j:j;if(planned[i])list.Add(i);}return list.ToArray();}
 static bool[] Selection(){return new[]{picks[0].IsChecked==true,picks[1].IsChecked==true,picks[2].IsChecked==true};}
 static string Names(int[] indexes){var names=new List<string>();foreach(int i in indexes)names.Add(Titles[i]);return string.Join("、",names.ToArray());}
 static void Buttons(){if(picks==null)return;int[] add=Plan(Selection(),states,false),del=Plan(Selection(),states,true);start.IsEnabled=!busy&&add.Length>0;uninstall.IsEnabled=!busy&&del.Length>0;start.Content=add.Length>0?"安装 / 更新所选 ("+add.Length+")":"安装所选";uninstall.Content=del.Length>0?"卸载所选 ("+del.Length+")":"卸载所选";
 ((TextBlock)window.FindName("SelectionInfo")).Text=add.Length==0&&del.Length==0?"请勾选需要操作的组件":(add.Length>0?"安装顺序："+Names(add):"")+(add.Length>0&&del.Length>0?"\n":"")+(del.Length>0?"卸载顺序："+Names(del):"");}
 static void Cards(string root){int[] found=new int[3];for(int i=0;i<3;i++)found[i]=State(root,packages[i]);window.Dispatcher.Invoke((Action)(()=>{
 states=found;managed=found[0]!=0||found[1]!=0||found[2]!=0;
 for(int i=0;i<3;i++){badges[i].Text=new[]{"未安装","已安装","安装不完整","清单异常","可更新"}[found[i]];badges[i].Foreground=(System.Windows.Media.Brush)new System.Windows.Media.BrushConverter().ConvertFromString(found[i]==1?"#157D70":found[i]==0?"#637B78":"#AC5B22");
 ((TextBlock)window.FindName("Hint"+i)).Text=(i==0?"必备组件":"可选组件")+" · 发布版本 "+packages[i].Version+(found[i]==1?" · 已安装":"");}
 start.Visibility=uninstall.Visibility=Visibility.Visible;((Button)window.FindName("OpenApp")).IsEnabled=!busy&&found[0]==1;Buttons();
 }));}
 static void EnableActions(bool enabled){scan.IsEnabled=enabled;folder.IsEnabled=mirror.IsEnabled=desktopShortcut.IsEnabled=enabled;((Button)window.FindName("Browse")).IsEnabled=enabled;foreach(var pick in picks)pick.IsEnabled=enabled;((Button)window.FindName("OpenApp")).IsEnabled=enabled&&states[0]==1;Buttons();if(!enabled)start.IsEnabled=uninstall.IsEnabled=false;}
 static void Detect(){if(busy)return;string root=folder.Text;Work(()=>{Recover(root);LoadFeed();Cards(root);Report(feedStatus,100);});}
 static void Batch(bool remove){if(busy)return;string root=folder.Text;int[] plan=Plan(Selection(),states,remove);if(plan.Length==0)return;bool mirrors=mirror.IsChecked==true,shortcut=desktopShortcut.IsChecked==true;
 if(remove&&MessageBox.Show("将按顺序卸载："+Names(plan)+"。\n用户文件和修改过的文件会保留。是否继续？","确认卸载所选组件",MessageBoxButton.YesNo,MessageBoxImage.Question)!=MessageBoxResult.Yes)return;
 Work(()=>{EnsureClosed(root);Recover(root);int kept=0;
 if(remove){foreach(int i in plan)kept+=Remove(root,packages[i]);if(!Has(root,packages[0])){Shortcut(root,Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),true);if(File.Exists(LastRoot)&&File.ReadAllText(LastRoot)==Path.GetFullPath(root))File.Delete(LastRoot);}Report("所选组件已卸载 · 用户文件已保留"+(kept>0?"，另保留 "+kept+" 个修改文件":""),100);}
 else {var archives=new Dictionary<int,string>();BeginTransaction(root);
 try{foreach(int i in plan){RequireCore(root,i);if(i>0)Health(root);archives[i]=Download(packages[i],mirrors);Extract(archives[i],root,packages[i]);if(i==0)Health(root);}CommitTransaction();}
 catch{RollbackTransaction();throw;}
 File.WriteAllText(LastRoot,Path.GetFullPath(root));if(shortcut)Shortcut(root,Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),false);foreach(var file in archives.Values)if(File.Exists(file))File.Delete(file);Report("已完成安装："+Names(plan)+(shortcut?" · 桌面快捷方式已就绪":""),100);}
 window.Dispatcher.Invoke((Action)(()=>{foreach(var pick in picks)pick.IsChecked=false;}));});}
 static string Hash(Stream s){using(var h=SHA256.Create())return BitConverter.ToString(h.ComputeHash(s)).Replace("-","").ToLowerInvariant();}
 static string transactionRoot,backupRoot;static Dictionary<string,string> undo;static bool committed;
 static void Journal(){string path=Safe(transactionRoot,".installer-transaction.json"),temp=path+".tmp";File.WriteAllText(temp,Json().Serialize(new{backup=Path.GetFileName(backupRoot),committed=committed}));if(File.Exists(path))File.Replace(temp,path,null);else File.Move(temp,path);}
 static void BeginTransaction(string root){transactionRoot=Path.GetFullPath(root);Directory.CreateDirectory(transactionRoot);backupRoot=Safe(transactionRoot,".installer-backup-"+Guid.NewGuid().ToString("N"));NoLinks(transactionRoot,backupRoot);Directory.CreateDirectory(backupRoot);undo=new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase);committed=false;File.WriteAllText(Safe(transactionRoot,".installer-undo.jsonl"),"");Journal();}
 static void Track(string relative){if(undo==null)return;string p=Safe(transactionRoot,relative);NoLinks(transactionRoot,p);if(undo.ContainsKey(relative))return;string copy=null;if(File.Exists(p)){copy=undo.Count.ToString()+".bin";File.Copy(p,Safe(backupRoot,copy));}File.AppendAllText(Safe(transactionRoot,".installer-undo.jsonl"),Json().Serialize(new{path=relative,backup=copy})+"\n");undo.Add(relative,copy);}
 static void CleanupTransaction(){foreach(string backup in undo.Values)if(backup!=null){string p=Safe(backupRoot,backup);if(File.Exists(p))File.Delete(p);}if(Directory.Exists(backupRoot)&&Directory.GetFileSystemEntries(backupRoot).Length==0)Directory.Delete(backupRoot);File.Delete(Safe(transactionRoot,".installer-transaction.json"));File.Delete(Safe(transactionRoot,".installer-undo.jsonl"));undo=null;}
 static void CommitTransaction(){committed=true;Journal();CleanupTransaction();}
 static void RollbackTransaction(){if(undo==null)return;if(committed){CleanupTransaction();return;}Report("正在恢复更新前的文件……",-1);foreach(var item in undo){string p=Safe(transactionRoot,item.Key);NoLinks(transactionRoot,p);if(File.Exists(p))File.Delete(p);if(item.Value!=null){Directory.CreateDirectory(Path.GetDirectoryName(p));File.Copy(Safe(backupRoot,item.Value),p);}}CleanupTransaction();}
 static void Recover(string root){string path=Safe(root,".installer-transaction.json");if(!File.Exists(path))return;var d=Json().Deserialize<Dictionary<string,object>>(File.ReadAllText(path));string name=(string)d["backup"];if(!System.Text.RegularExpressions.Regex.IsMatch(name,"^\\.installer-backup-[a-f0-9]{32}$"))throw new IOException("恢复记录无效，已停止操作");transactionRoot=Path.GetFullPath(root);backupRoot=Safe(root,name);undo=new Dictionary<string,string>();foreach(string line in File.ReadLines(Safe(root,".installer-undo.jsonl"))){var e=Json().Deserialize<Dictionary<string,object>>(line);string relative=(string)e["path"];Safe(root,relative);string copy=e["backup"] as string;if(copy!=null&&!System.Text.RegularExpressions.Regex.IsMatch(copy,"^[0-9]+\\.bin$"))throw new IOException("恢复文件记录无效");undo.Add(relative,copy);}committed=(bool)d["committed"];if(committed)CleanupTransaction();else RollbackTransaction();}
 static string Hash(string p){using(var s=File.OpenRead(p))return Hash(s);}
 static string Safe(string root,string rel){string p=Path.GetFullPath(Path.Combine(root,rel.Replace('/',Path.DirectorySeparatorChar)));if(!p.StartsWith(Path.GetFullPath(root)+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase))throw new IOException("压缩包路径越界");if(p.Length>230&&!p.StartsWith(@"\\?\"))return p.StartsWith(@"\\")?@"\\?\UNC\"+p.Substring(2):@"\\?\"+p;return p;}
 static void CheckCancel(){if(cancelled)throw new OperationCanceledException("已取消。已安装文件保留；重新安装会跳过校验通过的文件。");}
 static void Report(string text,double value){if(window!=null)window.Dispatcher.BeginInvoke((Action)(()=>{status.Text=text;bar.IsIndeterminate=value<0;if(value>=0)bar.Value=value;}));else Console.WriteLine(text);}
 static bool Verify(string root,Package p){
 string receipt=Path.Combine(root,".installer-"+p.Key+".json");bool legacy=!File.Exists(receipt);
 if(legacy){receipt=Path.Combine(root,"MANIFEST-"+p.Key+".json");if(!File.Exists(receipt))return false;}
 var data=Json().Deserialize<Dictionary<string,object>>(File.ReadAllText(receipt));
 if(!legacy&&(string)data["packageHash"]!=p.Hash)return false;
 if(legacy&&!Current(root,p))return false;
 var files=(Dictionary<string,object>)data["files"];int n=0;
 foreach(var entry in files){CheckCancel();string expected=legacy?(string)((Dictionary<string,object>)entry.Value)["sha256"]:(string)entry.Value;string f=Safe(root,entry.Key);if(!File.Exists(f)||Hash(f)!=expected)return false;if(++n%300==0)Report("正在校验已安装的 "+p.Key+" · "+n+" / "+files.Count,100.0*n/files.Count);}
 return true;
 }
 static string Download(Package p,bool mirrors){
 string path=Path.Combine(cache,p.Name);if(File.Exists(path)){Report("校验已下载的 "+p.Key,-1);if(Hash(path)==p.Hash)return path;}
 string url=p.Url;var sources=new List<string>();if(mirrors){sources.Add("https://ghfast.top/"+url);sources.Add("https://gh-proxy.com/"+url);sources.Add("https://ghproxy.net/"+url);}sources.Add(url);
 string partial=path+".part";Exception last=null;
 foreach(string source in sources)for(int attempt=0;attempt<2;attempt++){
 CheckCancel();try{
 Report("连接 "+new Uri(source).Host+" · "+p.Key,-1);
 var req=(HttpWebRequest)WebRequest.Create(source);req.UserAgent="MoleculeStudio-Installer/1.0";req.Timeout=20000;req.ReadWriteTimeout=20000;
 using(var response=req.GetResponse())using(var input=response.GetResponseStream())using(var output=File.Create(partial)){
 byte[] buffer=new byte[1024*1024];long done=0;int n;var time=Stopwatch.StartNew();long lastUpdate=0;
 while((n=input.Read(buffer,0,buffer.Length))>0){CheckCancel();output.Write(buffer,0,n);done+=n;if(time.ElapsedMilliseconds-lastUpdate>400){Report("下载 "+p.Key+" · "+(done/1048576)+" MB / "+(response.ContentLength/1048576)+" MB · "+new Uri(source).Host,response.ContentLength>0?100.0*done/response.ContentLength:-1);lastUpdate=time.ElapsedMilliseconds;}}
 }
 Report("校验 "+p.Key+" 的 SHA-256",-1);if(Hash(partial)!=p.Hash)throw new IOException("文件校验失败");if(File.Exists(path))File.Delete(path);File.Move(partial,path);return path;
 }catch(OperationCanceledException){throw;}catch(Exception e){last=e;Report("当前来源失败，正在重试或切换来源……",-1);}}
 throw new IOException("下载失败，请检查网络后重试。"+(last==null?"":last.Message));
 }
 static string Duplicate(string root,string rel){
 const string a="ocsr_runtime/python/cpython-3.10-windows-x86_64-none/",b="ocsr_runtime/venv/Lib/site-packages/";
 if(rel.StartsWith(a))return Safe(root,"chemistry_python/"+rel.Substring(a.Length));
 if(rel.StartsWith(b))return Safe(root,"chemistry_python/Lib/site-packages/"+rel.Substring(b.Length));return null;
 }
 static void Extract(string zip,string root,Package package){
 var previous=Inventory(root,package);var records=new Dictionary<string,string>();using(var z=ZipFile.OpenRead(zip)){int n=0;
 foreach(var e in z.Entries){CheckCancel();if(e.FullName.EndsWith("/"))continue;if(!e.FullName.StartsWith("MoleculeStudio/"))throw new IOException("未知压缩包结构");
 string rel=e.FullName.Substring(15),dest=Safe(root,rel),expected;if(rel.StartsWith(".installer-",StringComparison.OrdinalIgnoreCase))throw new IOException("发布包包含保留的安装器记录路径");using(var stream=e.Open())expected=Hash(stream);records[rel]=expected;
 Directory.CreateDirectory(Path.GetDirectoryName(dest));
 if(File.Exists(dest)){
 if(Hash(dest)==expected){n++;continue;}
 if(undo==null||!previous.ContainsKey(rel)||Hash(dest)!=previous[rel])throw new IOException("文件已被修改或不属于此组件："+rel+"。已停止安装并保留原文件。");
 Track(rel);File.Delete(dest);
 }
 Track(rel);
 string duplicate=Duplicate(root,rel);
 if(duplicate!=null&&File.Exists(duplicate)&&new FileInfo(duplicate).Length==e.Length&&Hash(duplicate)==expected&&CreateHardLink(dest,duplicate,IntPtr.Zero)){saved+=e.Length;}
 else {string temp=dest+".installing";using(var input=e.Open())using(var output=File.Create(temp))input.CopyTo(output);File.Move(temp,dest);}
 if(++n%100==0)Report("安装 "+package.Key+" · "+n+" / "+z.Entries.Count,100.0*n/z.Entries.Count);
 }}
 if(undo!=null){var shared=new HashSet<string>(StringComparer.OrdinalIgnoreCase);foreach(var other in packages)if(other.Key!=package.Key)foreach(var f in Inventory(root,other).Keys)shared.Add(f);foreach(var old in previous){string f=Safe(root,old.Key);if(!records.ContainsKey(old.Key)&&!shared.Contains(old.Key)&&File.Exists(f)&&Hash(f)==old.Value){Track(old.Key);File.Delete(f);}}}
 Track(".installer-"+package.Key+".json");File.WriteAllText(Path.Combine(root,".installer-"+package.Key+".json"),Json().Serialize(new {packageHash=package.Hash,version=package.Version,files=records}));
 }
 static void Health(string root){
 string exe=Path.Combine(root,"chemistry_python/python.exe");if(!File.Exists(exe))throw new IOException("缺少化学解释器");
 var si=new ProcessStartInfo(exe,"-B -c \"from rdkit.Chem import AllChem,rdDistGeom;import torch,scipy,pandas;import sys;sys.path.append('chemistry_runtime/nmr-ml-site');from csp5 import predict_mols\""){UseShellExecute=false,CreateNoWindow=true,WorkingDirectory=root,RedirectStandardError=true};
 using(var p=Process.Start(si)){string error="";p.ErrorDataReceived+=(s,e)=>{if(e.Data!=null)error+=e.Data+"\n";};p.BeginErrorReadLine();if(!p.WaitForExit(120000)){p.Kill();throw new IOException("运行时自检超时");}p.WaitForExit();if(p.ExitCode!=0)throw new IOException("运行时自检失败，可能被系统策略拦截：\n"+error);}
 }
 static void Work(Action action){if(busy)return;string root=folder.Text;busy=true;cancelled=false;EnableActions(false);
 new Thread(()=>{try{action();}catch(Exception e){Report(e.Message,0);}finally{
 try{Cards(root);}catch{}window.Dispatcher.BeginInvoke((Action)(()=>{busy=false;EnableActions(true);}));
 }} ){IsBackground=true}.Start();}
 [STAThread] static int Main(string[] args){
 AppContext.SetSwitch("Switch.System.IO.UseLegacyPathHandling",false);AppContext.SetSwitch("Switch.System.IO.BlockLongPaths",false);
 ServicePointManager.SecurityProtocol=SecurityProtocolType.Tls12;cache=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"MoleculeStudioInstaller","cache");Directory.CreateDirectory(cache);
 if(args.Length>0&&args[0]!="--preview"){try{
 if(args[0]=="--feed-test"&&args.Length==2){var p=ParseFeed(File.ReadAllText(args[1]));if(p.Length!=3)return 40;bool rejected=false;try{ParseFeed(File.ReadAllText(args[1]).Replace("https://github.com/","https://evil.invalid/"));}catch{rejected=true;}return rejected?0:41;}
 if(args[0]=="--batch-test"){
 if(string.Join(",",Plan(new[]{false,true,true},new[]{0,0,0},false))!="0,1,2")return 42;
 if(string.Join(",",Plan(new[]{false,true,true},new[]{1,0,0},false))!="1,2")return 43;
 if(string.Join(",",Plan(new[]{true,false,false},new[]{1,1,1},true))!="2,1,0")return 44;
 if(Plan(new[]{false,false,false},new[]{1,1,1},false).Length!=0)return 45;
 if(string.Join(",",Plan(new[]{false,true,false},new[]{4,0,4},false))!="0,1,2")return 46;return 0;}
 if(args[0]=="--rollback-test"){
 string root=Path.Combine(Path.GetTempPath(),"MoleculeStudio-rollback-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(root);File.WriteAllText(Path.Combine(root,"owned.txt"),"old");File.WriteAllText(Path.Combine(root,"user.txt"),"keep");
 var p=new Package("Core","fixture.zip",packages[0].Hash);File.WriteAllText(Path.Combine(root,".installer-Core.json"),Json().Serialize(new{packageHash=p.Hash,files=new Dictionary<string,string>{{"owned.txt",Hash(Path.Combine(root,"owned.txt"))}}}));
 string zip=Path.Combine(root,"fixture.zip");using(var z=ZipFile.Open(zip,ZipArchiveMode.Create)){using(var w=new StreamWriter(z.CreateEntry("MoleculeStudio/owned.txt").Open()))w.Write("new");using(var w=new StreamWriter(z.CreateEntry("MoleculeStudio/added.txt").Open()))w.Write("added");}
 BeginTransaction(root);Extract(zip,root,p);undo=null;Recover(root);if(File.ReadAllText(Path.Combine(root,"owned.txt"))!="old"||File.Exists(Path.Combine(root,"added.txt")))return 47;
 BeginTransaction(root);Extract(zip,root,p);CommitTransaction();if(File.ReadAllText(Path.Combine(root,"owned.txt"))!="new"||File.ReadAllText(Path.Combine(root,"user.txt"))!="keep")return 48;return Verify(root,p)?0:49;
 }
 if(args[0]=="--shortcut-test"){
 string t=Path.Combine(Path.GetTempPath(),"MoleculeStudio-shortcut-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(t);string desktop=Path.Combine(t,"Desktop"),root=Path.Combine(t,"中文 安装目录");Directory.CreateDirectory(root);File.Copy(Assembly.GetExecutingAssembly().Location,Path.Combine(root,"MoleculeStudio.exe"));
 Shortcut(root,desktop,false);string link=ShortcutPath(root,desktop,false);if(!File.Exists(link))return 20;byte[] before=File.ReadAllBytes(link);Shortcut(root,desktop,false);if(!Convert.ToBase64String(before).Equals(Convert.ToBase64String(File.ReadAllBytes(link))))return 21;
 Shortcut(Path.Combine(t,"other"),desktop,true);if(!File.Exists(link))return 22;Shortcut(root,desktop,true);if(File.Exists(link))return 23;
 // Another installation owns the normal name: create a distinct shortcut, never overwrite it.
 string other=Path.Combine(t,"other");Directory.CreateDirectory(other);File.Copy(Assembly.GetExecutingAssembly().Location,Path.Combine(other,"MoleculeStudio.exe"));Shortcut(other,desktop,false);Shortcut(root,desktop,false);if(!File.Exists(ShortcutPath(root,desktop,true)))return 24;Shortcut(root,desktop,true);if(!File.Exists(link)||File.Exists(ShortcutPath(root,desktop,true)))return 25;Shortcut(other,desktop,true);return 0;
 }
 if(args[0]=="--dependency-test"){
 string t=Path.Combine(Path.GetTempPath(),"MoleculeStudio-dependency-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(t);
 if(State(t,packages[0])!=0||CanInstall(1,0)||CanInstall(2,2)||!CanInstall(0,0))return 30;
 bool refused=false;try{RequireCore(t,1);}catch(IOException){refused=true;}if(!refused)return 31;
 var record=new Dictionary<string,string>{{"core.bin","unused"}};File.WriteAllText(Path.Combine(t,".installer-Core.json"),Json().Serialize(new{packageHash=packages[0].Hash,files=record}));if(State(t,packages[0])!=2)return 32;
 File.WriteAllText(Path.Combine(t,"core.bin"),"fixture");if(State(t,packages[0])!=1||!CanInstall(2,1))return 33;RequireCore(t,2);return 0;
 }
 if(args[0]=="--lifecycle-test"){
 string t=Path.Combine(Path.GetTempPath(),"MoleculeStudio-lifecycle-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(t);
 File.WriteAllText(Path.Combine(t,"shared.dll"),"shared");File.WriteAllText(Path.Combine(t,"ai.dll"),"ai");File.WriteAllText(Path.Combine(t,"settings.ini"),"original");File.WriteAllText(Path.Combine(t,"user.cdxml"),"drawing");
 var core=new Dictionary<string,string>{{"shared.dll",Hash(Path.Combine(t,"shared.dll"))}};
 var addon=new Dictionary<string,string>{{"shared.dll",Hash(Path.Combine(t,"shared.dll"))},{"ai.dll",Hash(Path.Combine(t,"ai.dll"))},{"settings.ini",Hash(Path.Combine(t,"settings.ini"))}};
 File.WriteAllText(Path.Combine(t,".installer-Core.json"),Json().Serialize(new{packageHash=packages[0].Hash,files=core}));File.WriteAllText(Path.Combine(t,".installer-AI.json"),Json().Serialize(new{packageHash=packages[1].Hash,files=addon}));
 File.WriteAllText(Path.Combine(t,"settings.ini"),"user changed");
 bool coreProtected=false;try{Remove(t,packages[0]);}catch(IOException){coreProtected=true;}if(!coreProtected||!File.Exists(Path.Combine(t,"shared.dll")))return 15;
 if(Remove(t,packages[1])!=1||!File.Exists(Path.Combine(t,"shared.dll"))||File.Exists(Path.Combine(t,"ai.dll"))||Has(t,packages[1]))return 10;
 if(!Verify(t,packages[0]))return 11;
 Remove(t,packages[0]);if(File.Exists(Path.Combine(t,"shared.dll"))||!File.Exists(Path.Combine(t,"user.cdxml"))||!File.Exists(Path.Combine(t,"settings.ini")))return 12;
 // Adding the component again must recreate only its owned files.
 string zip=Path.Combine(t,"addon.zip");using(var z=ZipFile.Open(zip,ZipArchiveMode.Create))using(var w=new StreamWriter(z.CreateEntry("MoleculeStudio/ai.dll").Open()))w.Write("ai");Extract(zip,t,packages[1]);if(!Verify(t,packages[1]))return 13;Remove(t,packages[1]);return File.Exists(Path.Combine(t,"user.cdxml"))?0:14;
 }
 if(args[0]=="--self-test"){
 bool rejected=false;try{Safe(cache,"../../escape");}catch(IOException){rejected=true;}if(!rejected)return 2;
 string t=Path.Combine(Path.GetTempPath(),"MoleculeStudio-installer-test-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(t);string z=Path.Combine(t,"fixture.zip"),dest=Path.Combine(t,"installed");Directory.CreateDirectory(dest);
 using(var archive=ZipFile.Open(z,ZipArchiveMode.Create)){foreach(string rel in new[]{"chemistry_python/Lib/site-packages/example.txt","ocsr_runtime/venv/Lib/site-packages/example.txt"}){using(var w=new StreamWriter(archive.CreateEntry("MoleculeStudio/"+rel).Open()))w.Write("same immutable content");}}
 var fixture=new Package("Test","fixture.zip","fixture");Extract(z,dest,fixture);if(!Verify(dest,fixture)||saved==0)return 3;Extract(z,dest,fixture);
 File.WriteAllText(Path.Combine(dest,"chemistry_python/Lib/site-packages/example.txt"),"modified");if(Verify(dest,fixture))return 4;
 bool conflict=false;try{Extract(z,dest,fixture);}catch(IOException){conflict=true;}return conflict?0:5;}
 if(args[0]=="--extract-test"&&args.Length==4){int i=int.Parse(args[1]);if(Hash(args[2])!=packages[i].Hash)throw new IOException("hash mismatch");Extract(args[2],args[3],packages[i]);if(!Verify(args[3],packages[i]))return 3;return 0;}
 if(args[0]=="--health-test"&&args.Length==2){Health(args[1]);return 0;}
 if(args[0]=="--network-test"){Download(new Package("Network test","MoleculeStudio-0.95-external-ai-skill.zip","1ce9a37f57069234d0e5c892c74e32cde8963bbb21d769155e69e11e88674a80"),true);return 0;}
 }catch(Exception e){Console.Error.WriteLine(e);return 1;}return 2;}
 var app=new Application();using(var stream=Assembly.GetExecutingAssembly().GetManifestResourceStream("Installer.xaml"))window=(Window)XamlReader.Load(stream);
 folder=(TextBox)window.FindName("Folder");status=(TextBlock)window.FindName("Status");bar=(ProgressBar)window.FindName("Progress");start=(Button)window.FindName("Start");scan=(Button)window.FindName("Scan");mirror=(CheckBox)window.FindName("Mirrors");
 uninstall=(Button)window.FindName("Uninstall");
 desktopShortcut=(CheckBox)window.FindName("DesktopShortcut");
 using(var icon=Assembly.GetExecutingAssembly().GetManifestResourceStream("App.ico")){window.Icon=System.Windows.Media.Imaging.BitmapFrame.Create(icon,System.Windows.Media.Imaging.BitmapCreateOptions.None,System.Windows.Media.Imaging.BitmapCacheOption.OnLoad);}
 folder.Text=File.Exists(LastRoot)?File.ReadAllText(LastRoot):Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"Programs","MoleculeStudio");
 picks=new CheckBox[3];badges=new TextBlock[3];for(int i=0;i<3;i++){picks[i]=(CheckBox)window.FindName("Pick"+i);badges[i]=(TextBlock)window.FindName("State"+i);picks[i].Checked+=(sender,e)=>Buttons();picks[i].Unchecked+=(sender,e)=>Buttons();}
 start.Click+=(sender,e)=>Batch(false);uninstall.Click+=(sender,e)=>Batch(true);
 ((Button)window.FindName("OpenApp")).Click+=(sender,e)=>{try{Process.Start(new ProcessStartInfo(Path.Combine(folder.Text,"MoleculeStudio.exe")){WorkingDirectory=folder.Text,UseShellExecute=true});}catch(Exception error){Report(error.Message,0);}};
 scan.Click+=(s,e)=>{string root=folder.Text;Work(()=>{var results=new List<string>();foreach(var p in packages){bool valid=false;try{valid=Verify(root,p);}catch(OperationCanceledException){throw;}catch{}results.Add(p.Key+"："+(valid?"已安装且完整":"未安装或需要校验安装"));}Report(string.Join("    ",results.ToArray()),100);});};
 ((Button)window.FindName("Cancel")).Click+=(s,e)=>{cancelled=true;Report("正在停止，请等待当前网络请求结束……",-1);};
 ((Button)window.FindName("Browse")).Click+=(s,e)=>{using(var dialog=new System.Windows.Forms.FolderBrowserDialog()){if(dialog.ShowDialog()==System.Windows.Forms.DialogResult.OK)folder.Text=dialog.SelectedPath;}};
 window.Closing+=(s,e)=>{if(busy){cancelled=true;e.Cancel=true;Report("正在停止，请稍后关闭窗口……",-1);}};
 if(args.Length>=2&&args[0]=="--preview"){
 if(args.Length>2)folder.Text=args[2];Cards(folder.Text);if(args.Length>3){picks[1].IsChecked=true;picks[2].IsChecked=true;}EnableActions(true);status.Text=feedStatus;
 window.Show();window.UpdateLayout();var bitmap=new System.Windows.Media.Imaging.RenderTargetBitmap((int)window.ActualWidth,(int)window.ActualHeight,96,96,System.Windows.Media.PixelFormats.Pbgra32);bitmap.Render(window);var encoder=new System.Windows.Media.Imaging.PngBitmapEncoder();encoder.Frames.Add(System.Windows.Media.Imaging.BitmapFrame.Create(bitmap));using(var f=File.Create(args[1]))encoder.Save(f);window.Close();return 0;}
 var detectionTimer=new System.Windows.Threading.DispatcherTimer{Interval=TimeSpan.FromMilliseconds(700)};detectionTimer.Tick+=(s,e)=>{if(busy)return;detectionTimer.Stop();Detect();};folder.TextChanged+=(s,e)=>{detectionTimer.Stop();EnableActions(false);detectionTimer.Start();};window.ContentRendered+=(s,e)=>Detect();
 EnableActions(false);app.Run(window);return 0;
 }
}
