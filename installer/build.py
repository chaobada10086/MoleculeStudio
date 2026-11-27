from pathlib import Path
import subprocess, json
root = Path(__file__).resolve().parents[1]
out = root / 'dist'
out.mkdir(exist_ok=True)
framework = Path('C:/Windows/Microsoft.NET/Framework64/v4.0.30319')
refs = ['PresentationFramework', 'PresentationCore', 'WindowsBase', 'System.Xaml',
        'System.IO.Compression', 'System.IO.Compression.FileSystem', 'System.Web.Extensions', 'System.Windows.Forms']
feed=root/"updates/stable.json"
json.loads(feed.read_text(encoding="utf8"))
subprocess.run([str(framework/'csc.exe'), '/nologo', '/target:winexe', '/platform:x64',
                '/win32manifest:'+str(root/'installer/app.manifest'),
                '/win32icon:'+str(root/'studio/assets/app-icons/moleculestudio-professional.ico'),
                '/resource:'+str(root/'studio/assets/app-icons/moleculestudio-professional.ico')+',App.ico',
                '/out:'+str(out/'MoleculeStudio-OnlineSetup.exe'), '/lib:'+str(framework/'WPF'),
                *['/r:'+name+'.dll' for name in refs],
                '/resource:'+str(feed)+',stable.json',
                '/resource:'+str(root/'installer/Installer.xaml')+',Installer.xaml',
                str(root/'installer/OnlineInstaller.cs')], check=True)
print(out/'MoleculeStudio-OnlineSetup.exe')
