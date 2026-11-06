// Companion to the three local release ZIPs. No downloads or system policy changes.
using System;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using System.Security.Cryptography;
using System.Web.Script.Serialization;
using System.Windows.Forms;
using System.Threading.Tasks;
using System.Collections.Generic;
using System.Diagnostics;

class OfflineInstall : Form {
    TextBox destination = new TextBox { Left=24, Top=65, Width=540 };
    CheckBox ai = new CheckBox { Left=24, Top=110, Width=230, Text="AI 助手（可选）", Checked=true };
    CheckBox ocsr = new CheckBox { Left=280, Top=110, Width=260, Text="图片识别（可选）", Checked=true };
    CheckBox shortcut = new CheckBox { Left=24, Top=145, Width=300, Text="创建桌面快捷方式", Checked=true };
    Button install = new Button { Left=24, Top=195, Width=160, Height=40, Text="开始离线安装" };
    ProgressBar progress = new ProgressBar { Left=24, Top=252, Width=540, Height=22, Minimum=0, Maximum=1000 };
    Label progressText = new Label { Left=24, Top=280, Width=550, Height=28, Text="等待开始" };
    Label status = new Label { Left=24, Top=312, Width=550, Height=70, Text="核心必装。读取同目录中的发布包，无需联网。\n请选择新的空文件夹，以免覆盖已有安装。" };
    bool busy;
    static string Long(string p) { p=Path.GetFullPath(p); return p.StartsWith(@"\\?\")?p:p.StartsWith(@"\\")?@"\\?\UNC\"+p.Substring(2):@"\\?\"+p; }
    static string Hash(Stream stream) { using(var sha=SHA256.Create()) return BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", "").ToLowerInvariant(); }
    static string HashFile(string p) { using(var f=File.OpenRead(Long(p))) return Hash(f); }
    static string HashFile(string p, Action<long,long> report) {
        using(var f=File.OpenRead(Long(p))) using(var sha=SHA256.Create()) {
            var buffer=new byte[1024*1024]; int n; long done=0, total=f.Length;
            while((n=f.Read(buffer,0,buffer.Length))>0) {
                sha.TransformBlock(buffer,0,n,buffer,0); done+=n; report(done,total);
            }
            sha.TransformFinalBlock(new byte[0],0,0); report(total,total);
            return BitConverter.ToString(sha.Hash).Replace("-", "").ToLowerInvariant();
        }
    }
    static Dictionary<string,object> ReadJson(string s) { return new JavaScriptSerializer { MaxJsonLength=int.MaxValue }.Deserialize<Dictionary<string,object>>(s); }
    static void Extract(string archive, string root, Action completed = null) {
        root=Path.GetFullPath(root).TrimEnd('\\')+"\\";
        using(var z=ZipFile.OpenRead(archive)) {
            string kind=new[]{"Core","AI","OCSR"}.Single(k=>z.GetEntry("MoleculeStudio/MANIFEST-"+k+".json")!=null);
            var manifest=z.GetEntry("MoleculeStudio/MANIFEST-"+kind+".json");
            if(manifest==null) throw new IOException("发布包缺少文件清单");
            Dictionary<string,object> files;
            using(var r=new StreamReader(manifest.Open())) files=(Dictionary<string,object>)ReadJson(r.ReadToEnd())["files"];
            var seen=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach(var e in z.Entries) {
                if(e.FullName.EndsWith("/")) continue;
                if(!e.FullName.StartsWith("MoleculeStudio/")) throw new IOException("发布包路径无效");
                string rel=e.FullName.Substring(15).Replace('/', '\\');
                string dest=Path.GetFullPath(Path.Combine(root,rel));
                if(!dest.StartsWith(root,StringComparison.OrdinalIgnoreCase)||rel.Contains(":" )||!seen.Add(dest)) throw new IOException("发布包路径越界或重复");
                for(string p=Path.GetDirectoryName(dest); p!=null&&p.Length>=root.TrimEnd('\\').Length;p=Path.GetDirectoryName(p))
                    if(Directory.Exists(Long(p))&&(File.GetAttributes(Long(p))&FileAttributes.ReparsePoint)!=0) throw new IOException("安装目录不能包含链接");
                string expected;
                if(e==manifest) { using(var f=e.Open()) expected=Hash(f); }
                else {
                    object value;
                    if(!files.TryGetValue(rel.Replace('\\','/'),out value)) throw new IOException("文件不在发布清单中");
                    expected=(string)((Dictionary<string,object>)value)["sha256"];
                }
                if(File.Exists(Long(dest))) {
                    if(HashFile(dest)==expected) { if(completed!=null) completed(); continue; }
                    throw new IOException("目标文件已存在且内容不同，请选择新的空目录："+rel);
                }
                Directory.CreateDirectory(Long(Path.GetDirectoryName(dest)));
                string temp=dest+".tmp-"+Guid.NewGuid().ToString("N");
                try {
                    using(var r=e.Open()) using(var w=File.Create(Long(temp))) r.CopyTo(w);
                    if(HashFile(temp)!=expected) throw new IOException("文件校验失败："+rel);
                    File.Move(Long(temp),Long(dest));
                } finally { if(File.Exists(Long(temp))) File.Delete(Long(temp)); }
                if(completed!=null) completed();
            }
            if(seen.Count!=files.Count+1) throw new IOException("发布包文件数量与清单不符");
        }
    }
    void Report(string phase, string count, long done, long total) {
        int value=total>0?(int)Math.Min(1000,done*1000.0/total):0;
        BeginInvoke((Action)(()=>{ status.Text=phase; progressText.Text=count; progress.Value=value; }));
    }
    void CreateShortcut(string root) {
        dynamic shell=Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell"));
        string path=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),"MoleculeStudio.lnk");
        if(File.Exists(path)) path=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),"MoleculeStudio-Offline-"+DateTime.Now.ToString("yyyyMMddHHmmss")+".lnk");
        dynamic link=shell.CreateShortcut(path);
        link.TargetPath=Path.Combine(root,"MoleculeStudio.exe"); link.WorkingDirectory=root; link.IconLocation=link.TargetPath+",0"; link.Save();
    }
    OfflineInstall() {
        Text="MoleculeStudio · 离线安装（支持长路径）"; Width=610; Height=440;
        Font=new System.Drawing.Font("Microsoft YaHei UI",10); StartPosition=FormStartPosition.CenterScreen;
        Controls.Add(new Label {Left=24,Top=25,Width=550,Text="安装位置（完整程序目录，可直接修改）"});
        destination.Text=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"Programs","MoleculeStudio-20261106");
        Controls.AddRange(new Control[]{destination,ai,ocsr,shortcut,install,progress,progressText,status});
        FormClosing+=(s,e)=>{if(busy)e.Cancel=true;};
        install.Click+=async(s,e)=> {
            string root;
            try {root=Path.GetFullPath(destination.Text);} catch {MessageBox.Show("安装位置无效");return;}
            bool withAi=ai.Checked,withOcsr=ocsr.Checked,withShortcut=shortcut.Checked;
            busy=true;install.Enabled=destination.Enabled=ai.Enabled=ocsr.Enabled=shortcut.Enabled=false;
            try {
                await Task.Run(()=> {
                    var json=new JavaScriptSerializer().DeserializeObject(File.ReadAllText(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"RELEASE_CHECKSUMS.json"))) as object[];
                    var packages=new List<KeyValuePair<string,string>>();
                    foreach(string kind in new[]{"Core","AI","OCSR"}) {
                        if(kind=="AI"&&!withAi||kind=="OCSR"&&!withOcsr)continue;
                        var row=json.Cast<Dictionary<string,object>>().Single(r=>(string)r["package"]==kind);
                        string name=(string)row["file"];
                        if(name!=Path.GetFileName(name))throw new IOException("发布包文件名无效");
                        string archive=Path.Combine(AppDomain.CurrentDomain.BaseDirectory,name);
                        packages.Add(new KeyValuePair<string,string>(kind,archive));
                    }
                    var timer=Stopwatch.StartNew();
                    foreach(var package in packages) {
                        var row=json.Cast<Dictionary<string,object>>().Single(r=>(string)r["package"]==package.Key);
                        Report("正在校验 "+package.Key+" 压缩包，安装前请稍候。","0 / 0 MB",0,1);
                        if(HashFile(package.Value,(done,total)=>{
                            if(timer.ElapsedMilliseconds>=100 || done==total) {
                                Report("正在校验 "+package.Key+" 压缩包，安装前请稍候。",
                                    String.Format("{0:F1} / {1:F1} MB",done/1048576.0,total/1048576.0),done,total);
                                timer.Restart();
                            }
                        })!=(string)row["sha256"])throw new IOException("发布包校验失败："+Path.GetFileName(package.Value));
                    }
                    int totalFiles=0, doneFiles=0;
                    foreach(var package in packages) using(var z=ZipFile.OpenRead(package.Value)) totalFiles+=z.Entries.Count(entry=>!entry.FullName.EndsWith("/"));
                    Report("开始安装组件…",String.Format("{0:N0} / {1:N0} 个文件",0,totalFiles),0,totalFiles);
                    foreach(var package in packages) {
                        string label=package.Key=="Core"?"核心工作台":package.Key=="AI"?"AI 助手":"图片识别";
                        Extract(package.Value,root,()=>{
                            doneFiles++;
                            if(timer.ElapsedMilliseconds>=100 || doneFiles==totalFiles) {
                                Report("正在安装："+label,String.Format("{0:N0} / {1:N0} 个文件",doneFiles,totalFiles),doneFiles,totalFiles);
                                timer.Restart();
                            }
                        });
                    }
                });
                if(withShortcut)CreateShortcut(root);
                progress.Value=progress.Maximum;
                status.Text="安装完成："+root+(withShortcut?"\n可通过桌面快捷方式启动。":"");
                MessageBox.Show(this, "MoleculeStudio 已成功安装！\n\n安装位置："+root+
                    (withShortcut?"\n\n已创建桌面快捷方式。":""), "安装成功", MessageBoxButtons.OK, MessageBoxIcon.Information);
            } catch(Exception ex) {status.Text="安装未完成："+ex.Message+"\n已完成文件保留，可修正后重试。";}
            finally {busy=false;install.Enabled=destination.Enabled=ai.Enabled=ocsr.Enabled=shortcut.Enabled=true;}
        };
    }
    [STAThread] static int Main(string[] args) {
        AppContext.SetSwitch("Switch.System.IO.UseLegacyPathHandling",false);
        AppContext.SetSwitch("Switch.System.IO.BlockLongPaths",false);
        if(args.Length==3&&args[0]=="--extract-test") {try{Extract(args[1],args[2]);return 0;}catch(Exception ex){File.WriteAllText(args[2]+"-error.txt",ex.ToString());return 1;}}
        if(args.Length==4&&args[0]=="--progress-test") {try{int n=0;Extract(args[1],args[2],()=>n++);File.WriteAllText(args[3],n.ToString());return 0;}catch{return 1;}}
        Application.EnableVisualStyles(); Application.Run(new OfflineInstall());return 0;
    }
}
