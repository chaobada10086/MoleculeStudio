"""Build the local release companion; no network or external build dependencies."""
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
(root/'dist').mkdir(exist_ok=True)
refs = ['System.Windows.Forms', 'System.Drawing', 'System.IO.Compression',
        'System.IO.Compression.FileSystem', 'System.Web.Extensions', 'Microsoft.CSharp']
subprocess.run(['C:/Windows/Microsoft.NET/Framework64/v4.0.30319/csc.exe',
                '/nologo', '/target:winexe', '/out:'+str(root/'dist/MoleculeStudio-OfflineInstall.exe'),
                '/win32icon:'+str(root/'studio/assets/app-icons/moleculestudio-professional.ico'),
                *['/r:'+r+'.dll' for r in refs], str(root/'installer/OfflineInstall.cs')], check=True)
