#!/usr/bin/env python3
import os,sys,json,shutil,subprocess as P,tarfile as A,zipfile as Z,urllib.request as U,ssl,time,threading as TH,stat
from contextlib import contextmanager as cm
W=sys.platform=='win32'
R=os.path.dirname(os.path.abspath(__file__));S=os.path.join(R,'src')
TD=os.path.join(R,'.tcc');CA=os.path.join(os.path.expanduser('~'),'.vulpin','src-cache')
RP='vulpin-lang/Vulpin';GU=f'https://github.com/{RP}.git';CH=1048576
CPU=os.cpu_count()or 4
TCC={'win':'https://download.savannah.gnu.org/releases/tinycc/tcc-0.9.27-win64-bin.zip',
     'linux':'https://download.savannah.gnu.org/releases/tinycc/tcc-0.9.27.tar.bz2'}
TCC_ALT={'win':['https://bellard.org/tcc/tcc-0.9.26-win64-bin.zip',
                'https://repo.or.cz/tinycc.git/snapshot/refs/tags/release_0_9_27.tar.gz',
                'https://codeload.github.com/TinyCC/tinycc/tar.gz/refs/tags/mob',
                'https://github.com/skeeto/w64devkit/releases/download/v2.9.1/w64devkit-x64-2.9.1.7z.exe',
                'https://bellard.org/tcc/tcc-0.9.27-win64-bin.zip'],
         'linux':['https://bellard.org/tcc/tcc-0.9.26.tar.bz2',
                  'https://download.savannah.gnu.org/releases/tinycc/tcc-0.9.26.tar.bz2',
                  'https://repo.or.cz/tinycc.git/snapshot/refs/tags/release_0_9_27.tar.gz',
                  'https://codeload.github.com/TinyCC/tinycc/tar.gz/refs/tags/mob',
                  'https://bellard.org/tcc/tcc-0.9.27.tar.bz2']}
C=dict(b='\033[38;5;39m',B='\033[38;5;33m',o='\033[38;5;208m',g='\033[38;5;46m',r='\033[38;5;196m',
       d='\033[38;5;240m',w='\033[38;5;255m',X='\033[0m',Z='\033[1m')
if os.environ.get('NO_COLOR')or not sys.stdout.isatty():
 for k in C:C[k]=''
elif W:
 try:
  import ctypes;ctypes.windll.kernel32.SetConsoleMode(ctypes.windll.kernel32.GetStdHandle(-11),7)
 except:pass
_SP=[0];_ST=[None];_SF=['[ … ]','[…  ]','[  …]']
def sp(t):
 _SP[0]=1;i=[0];t0=time.time()
 def r():
  while _SP[0]:
   el=int(time.time()-t0)
   sys.stdout.write(f"\r  {C['b']}{C['Z']}»{C['X']}  {t} {C['o']}{_SF[i[0]%3]}{C['X']}  {C['d']}{el}s{C['X']}   ")
   sys.stdout.flush();i[0]+=1;time.sleep(.15)
  sys.stdout.write('\r'+' '*90+'\r');sys.stdout.flush()
 th=TH.Thread(target=r,daemon=1);_ST[0]=th;th.start()
def spx():
 _SP[0]=0
 if _ST[0]:
  try:_ST[0].join(timeout=.5)
  except:pass
@cm
def _s(t):
 sp(t)
 try:yield
 finally:spx()
def o(s=''):sys.stdout.write(s+'\n');sys.stdout.flush()
def st(m):spx();o(f"  {C['b']}{C['Z']}»{C['X']}  {m}")
def ok(m):spx();o(f"  {C['g']}{C['Z']}✓{C['X']}  {m}")
def er(m):spx();o(f"  {C['r']}{C['Z']}✗{C['X']}  {m}")
def wn(m):spx();o(f"  {C['o']}{C['Z']}!{C['X']}  {m}")
def inf(m):spx();o(f"  {C['o']}{C['Z']}→{C['X']}  {m}")
def dm(m):spx();o(f"  {C['d']}{m}{C['X']}")
def rl():spx();o(f"  {C['B']}{'─'*60}{C['X']}")
def pz():
 try:input(f"\n  {C['d']}press enter…{C['X']}")
 except:pass
def ak(p,d=None):
 spx();x=f" {C['d']}[{d}]{C['X']}"if d not in(None,'')else''
 sys.stdout.write(f"  {C['b']}{C['Z']}?{C['X']}  {p}{x}: ");sys.stdout.flush()
 try:v=input().strip()
 except:v=''
 return v if v else(d or'')
def yn(p,d=0):
 x='Y/n'if d else'y/N';v=ak(p,x).lower()
 return d if v==x.lower() else v in('y','yes','1')
def _fmt(n):return f'{n}B'if n<1024 else(f'{n//1024}K'if n<1048576 else f'{n/1048576:.1f}M')
def pb(g,t):
 if t<=0:return
 p=int(g*100/t);w=30;f=int(p*w/100)
 sys.stdout.write(f"\r  {C['b']}{C['Z']}»{C['X']}  {C['o']}{'█'*f}{'░'*(w-f)}{C['X']}  {C['w']}{p:3d}%{C['X']}  {C['d']}{_fmt(g)}/{_fmt(t)}{C['X']}   ")
 sys.stdout.flush()
def _size(u):
 try:
  rq=U.Request(u,headers={'User-Agent':'vulpin','Range':'bytes=0-0','Accept-Encoding':'identity'})
  with U.urlopen(rq,timeout=15,context=ssl.create_default_context())as r:
   cr=r.headers.get('Content-Range','')
   if'/'in cr:return int(cr.split('/')[-1])
   return int(r.headers.get('Content-Length')or 0)
 except:return 0
def dl(u,pat):
 sz=_size(u)
 h={'User-Agent':'Mozilla/5.0 (vulpin-setup)'}
 if sz and sz<4*1048576:
  with U.urlopen(U.Request(u,headers=h),timeout=60,context=ssl.create_default_context())as r,open(pat,'wb')as f:
   tot=int(r.headers.get('Content-Length')or 0);g=0
   while 1:
    b=r.read(CH)
    if not b:break
    f.write(b);g+=len(b)
    if tot:pb(g,tot)
  sys.stdout.write('\r'+' '*90+'\r');sys.stdout.flush();return
 for c in(['curl','-fL','--retry','2','--connect-timeout','20','-o',pat,u],['wget','-q','-O',pat,u]):
  if shutil.which(c[0]):
   try:P.run(c,check=1);return
   except:pass
 rq=U.Request(u,headers=h)
 with U.urlopen(rq,timeout=60,context=ssl.create_default_context())as r,open(pat,'wb')as f:
  tot=int(r.headers.get('Content-Length')or 0);g=0
  while 1:
   b=r.read(CH)
   if not b:break
   f.write(b);g+=len(b)
   if tot:pb(g,tot)
 sys.stdout.write('\r'+' '*90+'\r');sys.stdout.flush()
def _ex(a,d):
 if a.endswith(('.tar.bz2','.tar.gz','.tgz','.tar.xz')):
  if shutil.which('tar'):P.run(['tar','-xf',a,'-C',d],check=1);return
  with A.open(a,'r:*')as t:t.extractall(d)
 elif a.endswith('.zip'):
  if shutil.which('unzip'):P.run(['unzip','-q','-o',a,'-d',d],check=1);return
  with Z.ZipFile(a)as z:z.extractall(d)
 elif a.endswith(('.7z','.7z.exe','.exe')):
  if shutil.which('7z'):P.run(['7z','x','-y','-o'+d,a],check=1);return
  P.run([a,'-y','-o'+d],check=1)
 else:raise RuntimeError(f'unknown archive: {a}')
def hs(d):return all(os.path.exists(os.path.join(d,'src',f))for f in('vulpin.c','vm.c'))
def db():
 try:
  rq=U.Request(f'https://api.github.com/repos/{RP}',headers={'User-Agent':'v'})
  with U.urlopen(rq,timeout=10)as x:return json.loads(x.read().decode()).get('default_branch','General')
 except:return'General'
# ── ASCII ART BANNER ──
N="""⠀⠀⣀⣤⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⢼⣿⠋⣡⣴⣶⠶⠶⠶⠶⣤⣀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠈⢿⣧⣀⠈⢿⣆⠀⠀⠀⠀⠙⠻⣦⡀⠀⠀⠀⠀⠀⠀⣠⣴⠾⠛⠛⠛⠉⠀
⠀⠀⢠⡈⠛⢿⣾⣿⣦⡀⠀⠀⠀⠀⠈⢿⣄⠀⠀⠀⢠⡾⠋⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠘⣧⡀⠀⠈⠙⢿⣿⣦⡀⠀⠀⠀⠈⢿⡄⠀⢠⣿⠁⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠘⠁⠀⠀⠀⠀⠙⢿⣿⣦⡀⠀⠀⢸⣷⠀⢸⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠙⢿⣿⣦⡀⠸⡟⠀⠘⣿⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠙⢿⣿⣦⡀⠀⠀⠹⣧⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⢀⣠⣴⠶⠶⠶⠶⠶⣶⣤⣶⠶⠄⠙⢿⣿⣦⡀⠀⠹⣧⠀⠀⠀⠀⠀⠀⠀
⠀⢠⡿⠉⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠙⢿⣿⣦⡀⠻⠀⠀⠀⠀⠀⠀⠀
⠀⢸⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠙⢿⣿⣦⡀⠀⠀⠀⠀⠀⠀
⠀⠈⢿⣄⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⠙⢿⣷⣄⠀⠀⠀⠀⠀
⠀⠀⠀⠙⢷⣤⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣀⣴⠿⠁⠀⠙⢿⣷⡄⠀⠀⠀
⠀⠀⠀⠀⠀⠈⠉⠛⠒⠶⠶⠶⠶⠶⠶⠶⠖⠛⠉⠁⠀⠀⠀⠀⠀⠈⠻⢦⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠑⠀""".split('\n')
def bn(s='Ninjutsu'):
 spx();o()
 for i,l in enumerate(N):o(f"  {C['b'] if i<8 else C['o']}{l}{C['X']}")
 o(f"\n  {C['d']}                v1.0.2  ·  {s}{C['X']}\n")
# ── SOURCE ──
def gs(force=0):
 if not force and hs(R):return S
 if not force and hs(CA):return os.path.join(CA,'src')
 g=shutil.which('git')
 if g:
  if os.path.isdir(os.path.join(CA,'.git')):
   with _s('updating cached source…'):
    r=P.run([g,'-C',CA,'fetch','--depth','1','--no-tags','origin'],capture_output=1,text=1)
    if r.returncode==0:
     br=P.run([g,'-C',CA,'rev-parse','--abbrev-ref','HEAD'],capture_output=1,text=1).stdout.strip()or'master'
     P.run([g,'-C',CA,'reset','--hard','origin/'+br],capture_output=1,text=1)
   if r.returncode:raise RuntimeError(f"git fetch failed:\n{r.stderr}")
  else:
   shutil.rmtree(CA,ignore_errors=1);os.makedirs(os.path.dirname(CA),exist_ok=1)
   with _s('cloning source…'):
    r=P.run([g,'clone','--depth','1','--single-branch','--no-tags',GU,CA],capture_output=1,text=1)
   if r.returncode:raise RuntimeError(f"git clone failed:\n{r.stderr}")
  return os.path.join(CA,'src')
 b=db();shutil.rmtree(CA,ignore_errors=1);os.makedirs(os.path.dirname(CA),exist_ok=1)
 a=CA+'.tar.gz'
 st(f'downloading source ({b})…');dl(f'https://github.com/{RP}/archive/refs/heads/{b}.tar.gz',a)
 with _s('extracting source…'):_ex(a,os.path.dirname(CA))
 os.remove(a)
 for x in os.listdir(os.path.dirname(CA)):
  px=os.path.join(os.path.dirname(CA),x)
  if os.path.isdir(px)and x.lower().startswith('vulpin')and px!=CA:shutil.move(px,CA);break
 if not hs(CA):raise RuntimeError('archive missing source files')
 return os.path.join(CA,'src')
# ── COMPILER ──
def fc():
 for n in('gcc','cc'):
  x=shutil.which(n)
  if x:return x,'gcc'
 for x in(os.path.join(TD,'bin','gcc.exe'),os.path.join(TD,'w64devkit','bin','gcc.exe')):
  if os.path.exists(x):return x,'gcc'
 for x in(os.path.join(TD,'tcc','tcc.exe'),os.path.join(TD,'tcc','tcc'),
         os.path.join(TD,'bin','tcc.exe'),os.path.join(TD,'bin','tcc'),
         os.path.join(TD,'tcc.exe'),os.path.join(TD,'tcc')):
  if os.path.exists(x):return x,'tcc'
 for r_,d_,fs_ in os.walk(TD):
  if r_.count(os.sep)-TD.count(os.sep)>3:continue
  for f_ in fs_:
   if f_.lower()in('tcc','tcc.exe'):return os.path.join(r_,f_),'tcc'
 return None,None
def dt():
 key='win'if W else'linux';u=[TCC[key]]+TCC_ALT[key]
 os.makedirs(TD,exist_ok=1);e=None
 for i,x in enumerate(u):
  a=os.path.join(TD,os.path.basename(x).split('?')[0]or f'tcc-{i}')
  try:
   sz=_size(x);tag=f" ({_fmt(sz)})"if sz else''
   st(f'tcc [{i+1}/{len(u)}]  {os.path.basename(a)[:44]}{tag}')
   dl(x,a)
  except Exception as y:
   e=y;wn(f'mirror {i+1} failed: {y}');continue
  try:
   with _s('extracting compiler…'):_ex(a,TD)
   os.remove(a)
   if not W:
    for r_,d_,fs_ in os.walk(TD):
     for f_ in fs_:
      if f_.lower()=='tcc':
       q=os.path.join(r_,f_)
       try:os.chmod(q,0o755)
       except:pass
   r=fc()
   if r[0]:ok(f'tcc via mirror {i+1}');return r
   e=RuntimeError('no compiler after extract')
  except Exception as y:e=y;wn(f'extract {i+1} failed: {y}');continue
 raise RuntimeError(f'all tcc mirrors failed. last: {e}')
# ── BUILD ──
def chx(p):
 try:os.chmod(p,os.stat(p).st_mode|stat.S_IXUSR|stat.S_IXGRP|stat.S_IXOTH)
 except:pass
def runs(p):
 if not p or not os.path.isfile(p):return False
 chx(p)
 for a in(['version'],['--version'],['-v'],['help']):
  try:
   r=P.run([p]+a,capture_output=1,text=1,timeout=8)
   if r.returncode==0 or r.stdout or r.stderr:return True
  except:pass
 return False
def wr():return'vulpin.bat'if W else'vulpin.sh'
def fw(src):
 for d in(os.path.join(src,'bin'),src,os.path.join(os.path.dirname(src),'bin'),
          os.path.dirname(src),R,os.path.join(R,'bin')):
  q=os.path.join(d,wr())
  if os.path.isfile(q):return q
 return None
def bd(force=0):
 src=gs(force);b='vulpin.exe'if W else'vulpin';p=os.path.join(src,b)
 w=fw(src)
 if w:chx(w)
 if not force and w and runs(w):
  inf(f'using wrapper: {w}');return w
 if os.path.exists(p)and not force:
  t=[os.path.getmtime(os.path.join(src,f))for f in('vulpin.c','vm.c')if os.path.exists(os.path.join(src,f))]
  if t and os.path.getmtime(p)>=max(t):
   chx(p);inf('binary up to date')
   if w:chx(w);return w
   return p
 c,ct=fc();m=os.path.join(src,'makefile')
 if os.path.exists(m)and c:
  e=os.environ.copy();e['CC']=c
  with _s(f'building with make -j{CPU}…'):
   r=P.run(['make','-C',src,f'-j{CPU}'],capture_output=1,text=1,env=e)
  if not r.returncode and os.path.exists(p):
   chx(p)
   if w:chx(w);return w
   return p
 files=[os.path.join(src,f)for f in('vulpin.c','vm.c')]
 mi=[f for f in files if not os.path.exists(f)]
 if mi:raise FileNotFoundError(f'missing: {mi}')
 if not c:
  st('no compiler found — installing tcc…');c,ct=dt()
  if not c:raise RuntimeError('failed to install tcc')
 e=os.environ.copy();cd=os.path.dirname(os.path.abspath(c))
 if cd not in e.get('PATH','').split(os.pathsep):e['PATH']=cd+os.pathsep+e.get('PATH','')
 with _s(f'compiling with {ct}…'):
  r=P.run([c,'-O2','-o',p]+files+['-lm'],capture_output=1,text=1,env=e)
 if r.returncode:raise RuntimeError(f'{ct} failed:\n{r.stderr}')
 chx(p)
 if w:
  chx(w)
  if runs(w):inf(f'using wrapper: {w}');return w
 return p
# ── INSTALL ──
def idir(cd=None):
 if cd:return os.path.abspath(cd)
 return os.path.join(os.path.expanduser('~'),'.local','Scripts'if W else'bin')
def rcs():
 h=os.path.expanduser('~');sh=os.path.basename(os.environ.get('SHELL','')or'')
 out=[]
 if sh=='zsh':out=[os.path.join(h,'.zshrc'),os.path.join(h,'.zprofile')]
 elif sh=='bash':out=[os.path.join(h,'.bashrc'),os.path.join(h,'.bash_profile')]
 elif sh=='fish':out=[os.path.join(h,'.config','fish','config.fish')]
 else:out=[os.path.join(h,'.profile')]
 for p in[os.path.join(h,'.zshrc'),os.path.join(h,'.zprofile'),
          os.path.join(h,'.bashrc'),os.path.join(h,'.bash_profile'),
          os.path.join(h,'.profile'),os.path.join(h,'.config','fish','config.fish')]:
  if p not in out:out.append(p)
 return out
def addpath(d):
 res=[];mk='# >>> vulpin setup >>>'
 for rc in rcs():
  if not os.path.exists(rc)and rc not in(os.path.join(os.path.expanduser('~'),'.zshrc'),
                                          os.path.join(os.path.expanduser('~'),'.bashrc')):continue
  try:
   body=''
   if os.path.exists(rc):
    with open(rc)as f:body=f.read()
   if mk in body:res.append((rc,0));continue
   os.makedirs(os.path.dirname(rc)or'.',exist_ok=1)
   with open(rc,'a')as f:
    if'fish'in rc:f.write(f'\n{mk}\nfish_add_path "{d}"\n# <<< vulpin setup <<<\n')
    else:f.write(f'\n{mk}\nexport PATH="{d}:$PATH"\n# <<< vulpin setup <<<\n')
   res.append((rc,1))
  except Exception as e:res.append((rc,f'err: {e}'))
 return res
def onpath(d):
 cur=(os.environ.get('PATH')or'').split(os.pathsep)
 if d in cur:ok(f'{d} already on PATH');return
 wn(f'{d} NOT on PATH')
 if yn(f'add {d} to shell rc files?',1):
  for rc,s in addpath(d):
   if s is True or s==1:ok(f'added to {rc}')
   elif s is False or s==0:inf(f'already in {rc}')
   else:wn(f'{rc}: {s}')
  sh=os.path.basename(os.environ.get('SHELL','')or'zsh')
  hint='fish_add_path $HOME/.local/bin'if sh=='fish'else'source ~/.zshrc && hash -r'
  o();inf('activate in THIS shell:');o(f"    {C['w']}{hint}{C['X']}")
  o(f"    {C['w']}vulpin version{C['X']}");o();inf('or open a new terminal')
def launcher(src,dst):
 ap=os.path.abspath(src);chx(ap)
 if W:
  with open(dst,'w',newline='')as f:f.write(f'@echo off\r\ncall "{ap}" %*\r\n')
 else:
  with open(dst,'w')as f:f.write(f'#!/bin/sh\nexec "{ap}" "$@"\n')
 chx(dst)
def search():
 found=[];seen=set()
 names=('vulpin','vulpin.exe','vulpin.cmd','vulpin.bat','vulpin.ps1','vulpin.sh')
 def add(p):
  if not p:return
  try:p=os.path.abspath(p)
  except:return
  if not os.path.isfile(p)or p in seen:return
  seen.add(p);found.append(p)
 h=os.path.expanduser('~')
 for k in(os.environ.get('PATH')or'').split(os.pathsep):
  if not k or not os.path.isabs(k)or not os.path.isdir(k):continue
  for fn in names:add(os.path.join(k,fn))
 for d in[os.path.join(h,'.local','bin'),os.path.join(h,'.local','Scripts'),
          os.path.join(h,'bin'),os.path.join(h,'.vulpin'),os.path.join(h,'.vulpin','bin'),
          CA,os.path.join(CA,'src'),os.path.join(CA,'src','bin'),os.path.join(CA,'bin'),
          S,os.path.join(S,'bin'),os.path.join(R,'bin'),R,
          os.path.join(sys.prefix,'bin'),os.path.join(sys.prefix,'Scripts')]:
  if not d:continue
  for fn in names:add(os.path.join(d,fn))
 for base in[os.path.join(h,'.vulpin'),R]:
  if not os.path.isdir(base):continue
  bd0=base.rstrip(os.sep).count(os.sep)
  for r_,ds,fs_ in os.walk(base):
   if r_.count(os.sep)-bd0>4:ds[:]=[];continue
   ds[:]=[x for x in ds if x not in('.git','__pycache__','node_modules','.tcc')]
   for f_ in fs_:
    if f_.lower()in names:add(os.path.join(r_,f_))
 tgt=os.path.abspath(os.path.join(h,'.local','bin'))
 def pref(p):
  b=os.path.basename(p).lower()
  wp=0 if b in('vulpin.sh','vulpin.bat','vulpin.cmd','vulpin.ps1')else 1
  rp=os.path.realpath(p)
  loc=0 if os.path.dirname(rp)==tgt else(1 if rp.startswith(os.path.join(h,'.vulpin'))else(2 if rp.startswith(os.path.abspath(R))else 3))
  return(wp,loc)
 found.sort(key=pref)
 return[(q,runs(q))for q in found]
def ib(p,cd=None):
 d=idir(cd)
 st('verifying…');chx(p)
 if not runs(p):
  er(f'does not run: {p}');er('aborting — old left untouched')
  raise RuntimeError('verification failed')
 ok('runs')
 for q,_ in search():
  if os.path.realpath(q)==os.path.realpath(p):continue
  try:os.remove(q);inf(f'removed old: {q}')
  except Exception as e:wn(f'cannot remove {q}: {e}')
 os.makedirs(d,exist_ok=1);x=os.path.join(d,'vulpin.bat'if W else'vulpin')
 if os.path.exists(x)or os.path.islink(x):
  try:os.remove(x)
  except:pass
 if p.endswith(('.sh','.bat')):
  inf(f'installing wrapper: {p}');launcher(p,x)
 else:
  shutil.copy2(p,x);chx(x)
 if not runs(x):raise RuntimeError(f'{x} does not run')
 ok(f'installed: {x}');onpath(d)
 return x
def di(force=0):
 st('building vulpin…'if not force else'refreshing source and rebuilding…')
 p=bd(force);st('installing…')
 return ib(p)
# ── FLOWS ──
def fi():
 o();bn('install')
 try:d=di();o();ok(f'installed: {d}');ok('done')
 except Exception as e:er(str(e))
 pz()
def fu():
 o();bn('update')
 try:d=di(1);o();ok(f'updated: {d}');ok('done')
 except Exception as e:er(str(e))
 pz()
def fx():
 o();bn('fix path')
 d=idir();x=os.path.join(d,'vulpin.bat'if W else'vulpin')
 inf('looking for vulpin…\n')
 found=search()
 if not found:
  er('nothing found')
  if yn('install now?',1):
   try:di();ok('done')
   except Exception as e:er(str(e))
  pz();return
 inf(f'found {len(found)}:')
 for q,g_ in found:
  tag=f"{C['g']}✓ runs{C['X']}"if g_ else f"{C['r']}✗ broken{C['X']}"
  mk=' ←'if os.path.abspath(q)==os.path.abspath(x)else''
  o(f"    {q}  {tag}{mk}")
 o()
 good=[q for q,g_ in found if g_]
 if not good:
  er('all broken — reinstall needed')
  if yn('install now?',1):
   try:di();ok('done')
   except Exception as e:er(str(e))
  pz();return
 src=os.path.abspath(good[0]);inf(f'using: {src}')
 os.makedirs(d,exist_ok=1)
 if os.path.abspath(src)!=os.path.abspath(x):
  if os.path.exists(x)or os.path.islink(x):
   try:os.remove(x)
   except Exception as e:er(f'cannot remove {x}: {e}');pz();return
  try:
   if src.endswith(('.sh','.bat')):launcher(src,x)
   else:shutil.copy2(src,x);chx(x)
   ok(f'installed: {x}')if runs(x)else er('does not run after install')
  except Exception as e:er(f'cannot place: {e}')
 else:chx(x);ok(f'already at: {x}')
 o();onpath(d);o()
 wh=shutil.which('vulpin')
 ok(f'vulpin → {wh}')if wh else wn('open a new shell so PATH refreshes')
 pz()
def fh():
 o();bn('help')
 o(f"  {C['o']}{C['Z']}usage{C['X']}")
 o(f"    {C['w']}python setup.py{C['X']}                {C['d']}menu{C['X']}")
 o(f"    {C['w']}python setup.py console{C['X']}        {C['d']}build & install{C['X']}")
 o(f"    {C['w']}python setup.py update{C['X']}         {C['d']}refresh & rebuild{C['X']}")
 o(f"    {C['w']}python setup.py fix{C['X']}            {C['d']}find + repair PATH{C['X']}")
 o(f"    {C['w']}python setup.py help{C['X']}           {C['d']}this help{C['X']}")
 o()
 o(f"  {C['o']}{C['Z']}pipeline{C['X']}")
 o(f"    {C['d']}1. clone/update {RP}{C['X']}")
 o(f"    {C['d']}2. find gcc, else download tcc{C['X']}")
 o(f"    {C['d']}3. make/chmod src/vulpin.c + vm.c → src/vulpin{C['X']}")
 o(f"    {C['d']}4. chmod +x bin/{wr()}{C['X']}")
 o(f"    {C['d']}5. install launcher ~/.local/bin/vulpin{C['X']}")
 o(f"    {C['d']}6. add ~/.local/bin to PATH{C['X']}")
 o()
 o(f"  {C['o']}{C['Z']}run{C['X']}   {C['w']}vulpin myprogram.vul{C['X']}\n")
def mm():
 while 1:
  sys.stdout.write('\033[2J\033[H');sys.stdout.flush()
  bn()
  o(f"  {C['o']}{C['Z']}[1]{C['X']}  {C['w']}Install{C['X']}      {C['d']}build & install vulpin{C['X']}")
  o(f"  {C['o']}{C['Z']}[2]{C['X']}  {C['w']}Update{C['X']}       {C['d']}refresh source & rebuild{C['X']}")
  o(f"  {C['o']}{C['Z']}[3]{C['X']}  {C['w']}Fix{C['X']}          {C['d']}find + repair PATH{C['X']}")
  o(f"  {C['o']}{C['Z']}[4]{C['X']}  {C['w']}Help{C['X']}         {C['d']}show usage{C['X']}")
  o(f"  {C['o']}{C['Z']}[0]{C['X']}  {C['w']}Exit{C['X']}")
  o();rl();o();ch=ak('select')
  if ch=='1':fi()
  elif ch=='2':fu()
  elif ch=='3':fx()
  elif ch=='4':fh();pz()
  elif ch in('0','q','quit','exit'):
   sys.stdout.write('\033[2J\033[H');sys.stdout.flush()
   o(f"  {C['o']}{C['Z']}[…] OK{C['X']}");return
try:from setuptools import setup,Command
except:
 print('installing setuptools…');P.check_call([sys.executable,'-m','pip','install','setuptools']);from setuptools import setup,Command
class _C(Command):
 user_options=[]
 def initialize_options(self):pass
 def finalize_options(self):pass
class CI(_C):
 description='Build and install vulpin'
 def run(self):
  o();bn('install')
  try:d=di();o();ok(f'installed: {d}');ok('done')
  except Exception as e:er(str(e));sys.exit(1)
class UP(_C):
 description='Refresh source & rebuild'
 def run(self):
  o();bn('update')
  try:d=di(1);o();ok(f'updated: {d}');ok('done')
  except Exception as e:er(str(e));sys.exit(1)
class FX(_C):
 description='Find + repair PATH'
 def run(self):fx()
class HP(_C):
 description='Show help'
 def run(self):fh()
_K={'console','update','fix','help','build','install','sdist','bdist_wheel','egg_info',
    'develop','clean','check','register','upload','test','dist_info','install_egg_info',
    'install_scripts','install_lib'}
if len(sys.argv)<2 or(sys.argv[1]not in _K and not sys.argv[1].startswith('-')):
 try:sys.stdout.reconfigure(encoding='utf-8',errors='replace')
 except:pass
 try:mm()
 except KeyboardInterrupt:o();wn('interrupted');sys.exit(130)
 sys.exit(0)
setup(name='vulpin',version='1.0.2',description='Vulpin programming language',
      packages=[],scripts=[],cmdclass={'console':CI,'update':UP,'fix':FX,'help':HP})
