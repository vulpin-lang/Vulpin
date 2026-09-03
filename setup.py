import os,sys,shutil,subprocess,threading,tarfile,zipfile,urllib.request
from setuptools import setup,Command
from distutils.command.install import install as _install
IS_WIN=sys.platform=='win32';PROJECT_ROOT=os.path.dirname(os.path.abspath(__file__));SRC_DIR=os.path.join(PROJECT_ROOT,'src')
TCC_URLS={'win64':'https://github.com/skeeto/w64devkit/releases/download/v2.9.1/w64devkit-x64-2.9.1.7z.exe','win32':'https://github.com/skeeto/w64devkit/releases/download/v2.9.1/w64devkit-x64-2.9.1.7z.exe','linux64':'https://bellard.org/tcc/tcc-0.9.27.tar.bz2'};TCC_DIR=os.path.join(PROJECT_ROOT,'.tcc')
def get_tcc_url():
 if IS_WIN:return TCC_URLS['win64'] if sys.maxsize>2**32 else TCC_URLS['win32']
 elif sys.platform=='linux':return TCC_URLS['linux64']
 return None
def find_compiler():
 for n in ['gcc','cc']:
  r=shutil.which(n)
  if r:return r,'gcc'
 for p in [os.path.join(TCC_DIR,'bin','gcc.exe'),os.path.join(TCC_DIR,'w64devkit','bin','gcc.exe')]:
  if os.path.exists(p):return p,'gcc'
 p=os.path.join(TCC_DIR,'bin','tcc')
 if os.path.exists(p):return p,'tcc'
 return None,None
def download_and_install_tcc(pc=None):
 url=get_tcc_url()
 if not url:raise RuntimeError(f"No TCC download available for platform: {sys.platform}")
 os.makedirs(TCC_DIR,exist_ok=True);ap=os.path.join(TCC_DIR,os.path.basename(url))
 if pc:pc("downloading compiler...")
 def rh(bn,bs,ts):
  if pc and ts>0:pc(f"downloading compiler... {int(min((bn*bs)/ts,1.0)*100)}%")
 urllib.request.urlretrieve(url,ap,reporthook=rh)
 if pc:pc("extracting compiler...")
 if ap.endswith(('.7z.exe','.exe')):subprocess.run([ap,'-y','-o'+TCC_DIR],check=True)
 elif ap.endswith('.zip'):
  with zipfile.ZipFile(ap,'r')as z:z.extractall(TCC_DIR)
 elif ap.endswith(('.tar.bz2','.tar.gz','.tgz')):
  with tarfile.open(ap,'r:*')as t:t.extractall(TCC_DIR)
 else:raise RuntimeError(f"Unknown archive format: {ap}")
 os.remove(ap)
 if not IS_WIN:
  tb=os.path.join(TCC_DIR,'bin','tcc')
  if os.path.exists(tb):os.chmod(tb,0o755)
 return find_compiler()
def build_vulpin(pc=None):
 bn='vulpin.exe' if IS_WIN else 'vulpin';bp=os.path.join(SRC_DIR,bn)
 if os.path.exists(bp):
  st=[os.path.getmtime(os.path.join(SRC_DIR,s))for s in ['vulpin.c','vm.c'] if os.path.exists(os.path.join(SRC_DIR,s))]
  if st and os.path.getmtime(bp)>=max(st):
   if pc:pc("binary up to date")
   return bp
 mf=os.path.join(SRC_DIR,'makefile');cp,ct=find_compiler()
 if mf and os.path.exists(mf) and cp:
  env=os.environ.copy();env['CC']=cp
  if pc:pc("building with makefile...")
  r=subprocess.run(['make','-C',SRC_DIR],capture_output=True,text=True,env=env)
  if r.returncode==0 and os.path.exists(bp):return bp
 src=[os.path.join(SRC_DIR,s)for s in ['vulpin.c','vm.c']]
 ms=[s for s in src if not os.path.exists(s)]
 if ms:raise FileNotFoundError(f"Missing source files: {ms}")
 if not cp:
  if pc:pc("no compiler found, installing tcc...")
  cp,ct=download_and_install_tcc(pc)
  if not cp:raise RuntimeError("Failed to install TCC compiler")
 if pc:pc(f"compiling with {ct}...")
 env=os.environ.copy();cd=os.path.dirname(os.path.abspath(cp))
 if cd not in env.get('PATH','').split(os.pathsep):env['PATH']=cd+os.pathsep+env.get('PATH','')
 cmd=[cp,'-O2','-o',bp]+src+['-lm']
 r=subprocess.run(cmd,capture_output=True,text=True,env=env)
 if r.returncode!=0:raise RuntimeError(f"{ct} failed:\n{r.stderr}")
 return bp
def install_binary(bp,isd):
 dn='vulpin.exe' if IS_WIN else 'vulpin';d=os.path.join(isd,dn)
 os.makedirs(isd,exist_ok=True)
 if os.path.exists(d):
  try:os.remove(d)
  except:pass
 shutil.copy2(bp,d)
 try:os.chmod(d,0o755)
 except:pass
 return d
class ConsoleInstallCommand(Command):
 description='Install Vulpin from console with progress output';user_options=[]
 def initialize_options(self):pass
 def finalize_options(self):pass
 def run(self):
  sys.stdout.reconfigure(encoding='utf-8',errors='replace')
  def log(m):print(f"[vulpin] {m}")
  log("building vulpin...")
  try:bp=build_vulpin(log)
  except TypeError:bp=build_vulpin()
  log("installing...")
  try:
   import site
   user_base=getattr(site,'USER_BASE',None)
   if user_base:
    scripts_dir=os.path.join(user_base,'bin')
    if not os.path.exists(scripts_dir):
     os.makedirs(scripts_dir,exist_ok=True)
    d=install_binary(bp,scripts_dir)
    log(f"installed vulpin binary to: {d}")
    log("done ✓")
    return
  except:pass
  try:
   self.distribution.run_command('build')
   ic=self.distribution.get_command_obj('install')
   ic.user=True
   ic.ensure_finalized()
   _install.run(ic)
   d=install_binary(bp,ic.install_scripts)
   log(f"installed vulpin binary to: {d}")
  except Exception as e:
   log(f"install failed: {e}, using fallback...")
   import site
   user_base=getattr(site,'USER_BASE',None)
   if user_base:
    scripts_dir=os.path.join(user_base,'bin')
    os.makedirs(scripts_dir,exist_ok=True)
    d=install_binary(bp,scripts_dir)
    log(f"installed vulpin binary to: {d}")
  log("done ✓")
class HelpCommand(Command):
 description='Show installation help';user_options=[]
 def initialize_options(self):pass
 def finalize_options(self):pass
 def run(self):
  print("""╔══════════════════════════════════════════════╗
║          Vulpin Installation Help            ║
╠══════════════════════════════════════════════╣
║                                              ║
║  Console Installer:                          ║
║    python setup.py console                   ║
║                                              ║
║  To run Vulpin after install:               ║
║    vulpin myprogram.vul                     ║
╚══════════════════════════════════════════════╝""")
class GuiInstallCommand(Command):
 description='Launch GUI installer for Vulpin';user_options=[]
 def initialize_options(self):pass
 def finalize_options(self):pass
 def run(self):
  try:import customtkinter as ctk
  except ImportError:
   print("customtkinter not found. Run: pip install customtkinter");print("Falling back to console install...\n");self.distribution.run_command('console');return
  ctk.set_appearance_mode("dark");ctk.set_default_color_theme("blue");app=ctk.CTk();app.title("Vulpin");app.geometry("400x440");app.resizable(False,False);app.configure(fg_color="#0d0d0d");app.update_idletasks();x=(app.winfo_screenwidth()//2)-200;y=(app.winfo_screenheight()//2)-220;app.geometry(f"+{x}+{y}");container=ctk.CTkFrame(app,fg_color="transparent");container.pack(expand=True,fill="both",padx=30,pady=30);logo=ctk.CTkLabel(container,text="vulpin 0.9",font=ctk.CTkFont(size=36,weight="bold",family="monospace"),text_color="#ffffff");logo.pack(pady=(0,4));tagline=ctk.CTkLabel(container,text="Wifi requirement, to download some packages.",font=ctk.CTkFont(size=12),text_color="#555555");tagline.pack(pady=(0,20));options_frame=ctk.CTkFrame(container,fg_color="#141414",corner_radius=10);options_frame.pack(pady=(0,20),fill="x",padx=10);docs_var=ctk.BooleanVar(value=True);docs_check=ctk.CTkCheckBox(options_frame,text="Include documentation",font=ctk.CTkFont(size=13),variable=docs_var,fg_color="#C35817",hover_color="#C06901",checkmark_color="#000000",text_color="#aaaaaa");docs_check.pack(anchor="w",padx=15,pady=(12,6));web_var=ctk.BooleanVar(value=True);web_check=ctk.CTkCheckBox(options_frame,text="Include website files",font=ctk.CTkFont(size=13),variable=web_var,fg_color="#C35817",hover_color="#C06901",checkmark_color="#000000",text_color="#aaaaaa");web_check.pack(anchor="w",padx=15,pady=(6,12));progress=ctk.CTkProgressBar(container,width=300,height=6,corner_radius=3,fg_color="#1a1a1a",progress_color="#C35817");progress.pack(pady=(0,15));progress.set(0);status=ctk.CTkLabel(container,text="",font=ctk.CTkFont(size=13),text_color="#666666");status.pack(pady=(0,20));btn=ctk.CTkButton(container,text="install",font=ctk.CTkFont(size=14,weight="bold"),width=160,height=42,corner_radius=21,fg_color="#C35817",hover_color="#C06901",text_color="#000000")
  def safe_update(f):app.after(0,f)
  def set_status(t,c="#666666"):safe_update(lambda:status.configure(text=t,text_color=c))
  def animate_progress(target,duration_ms=300):
   current=progress.get();steps=max(1,int(duration_ms/16));delta=(target-current)/steps
   for i in range(steps):
    val=current+delta*(i+1);safe_update(lambda v=val:progress.set(v));app.after(16)
  def do_install():
   safe_update(lambda:btn.configure(state="disabled",text="installing",fg_color="#333333"));safe_update(lambda:docs_check.configure(state="disabled"));safe_update(lambda:web_check.configure(state="disabled"))
   def worker():
    try:
     set_status("compiling sources...");animate_progress(0.15,300)
     try:bp=build_vulpin(set_status)
     except TypeError:bp=build_vulpin()
     set_status("installing runtime...");animate_progress(0.5,400)
     try:
      import site
      user_base=getattr(site,'USER_BASE',None)
      if user_base:
       scripts_dir=os.path.join(user_base,'bin')
       os.makedirs(scripts_dir,exist_ok=True)
       install_binary(bp,scripts_dir)
      else:
       self.distribution.run_command('build')
       ic=self.distribution.get_command_obj('install')
       ic.user=True
       ic.ensure_finalized()
       _install.run(ic)
       install_binary(bp,ic.install_scripts)
     except Exception as e:
      set_status(f"install fallback: {e}","#e0a040")
      import site
      user_base=getattr(site,'USER_BASE',None)
      if user_base:
       scripts_dir=os.path.join(user_base,'bin')
       os.makedirs(scripts_dir,exist_ok=True)
       install_binary(bp,scripts_dir)
     set_status("done ✓","#40d070");animate_progress(1.0,300);safe_update(lambda:btn.configure(text="complete",fg_color="#40d070",hover_color="#30c060"))
    except Exception as e:
     set_status(f"failed: {e}","#e04040");safe_update(lambda:btn.configure(state="normal",text="retry",fg_color="#e04040",hover_color="#d03030"));safe_update(lambda:docs_check.configure(state="normal"));safe_update(lambda:web_check.configure(state="normal"))
   threading.Thread(target=worker,daemon=True).start()
  btn.configure(command=do_install);btn.pack();app.mainloop()
setup(name='vulpin',version='0.9.5',description='Vulpin programming language',packages=[],scripts=[],cmdclass={'gui':GuiInstallCommand,'console':ConsoleInstallCommand,'help':HelpCommand})
