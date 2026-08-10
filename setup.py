import os
import sys
import shutil
import subprocess
import threading
import tarfile
import zipfile
import urllib.request
from setuptools import setup, Command
from distutils.command.install import install as _install

IS_WIN = sys.platform == 'win32'
PROJECT_ROOT = os.path.dirname(os.path.abspath(__file__))
SRC_DIR = os.path.join(PROJECT_ROOT, 'src')
DOCS_DIR = os.path.join(PROJECT_ROOT, 'docs')
WEB_DIR = os.path.join(PROJECT_ROOT, 'website')
LAUNCHER_SRC = os.path.join(PROJECT_ROOT, 'bin', 'vulpin.bat' if IS_WIN else 'vulpin.sh')
C_SOURCES = ['main.c', 'lexer.c', 'parser.c', 'vm.c', 'vulpin.c']

TCC_URLS = {
    'win64': 'https://github.com/skeeto/w64devkit/releases/download/v2.0.0/w64devkit-x64-2.0.0.zip',
    'win32': 'https://github.com/skeeto/w64devkit/releases/download/v2.0.0/w64devkit-i686-2.0.0.zip',
    'linux64': 'https://bellard.org/tcc/tcc-0.9.27.tar.bz2',
}
TCC_DIR = os.path.join(PROJECT_ROOT, '.tcc')


# compiler and build

def get_tcc_url():
    if IS_WIN:
        return TCC_URLS['win64'] if sys.maxsize > 2**32 else TCC_URLS['win32']
    elif sys.platform == 'linux':
        return TCC_URLS['linux64']
    return None


def find_compiler():
    for name in ['gcc', 'cc']:
        result = shutil.which(name)
        if result:
            return result, 'gcc'
    tcc_path = os.path.join(TCC_DIR, 'bin', 'gcc.exe' if IS_WIN else 'tcc')
    if os.path.exists(tcc_path):
        return tcc_path, 'tcc'
    return None, None


def download_and_install_tcc(progress_callback=None):
    url = get_tcc_url()
    if not url:
        raise RuntimeError(f"No TCC download available for platform: {sys.platform}")

    os.makedirs(TCC_DIR, exist_ok=True)
    archive_path = os.path.join(TCC_DIR, os.path.basename(url))

    if progress_callback:
        progress_callback("downloading compiler...")

    def report_hook(block_num, block_size, total_size):
        if progress_callback and total_size > 0:
            pct = min((block_num * block_size) / total_size, 1.0)
            progress_callback(f"downloading compiler... {int(pct * 100)}%")

    urllib.request.urlretrieve(url, archive_path, reporthook=report_hook)

    if progress_callback:
        progress_callback("extracting compiler...")

    if archive_path.endswith('.zip'):
        with zipfile.ZipFile(archive_path, 'r') as z:
            z.extractall(TCC_DIR)
    elif archive_path.endswith(('.tar.bz2', '.tar.gz', '.tgz')):
        with tarfile.open(archive_path, 'r:*') as t:
            t.extractall(TCC_DIR)
    else:
        raise RuntimeError(f"Unknown archive format: {archive_path}")

    os.remove(archive_path)

    if not IS_WIN:
        tcc_bin = os.path.join(TCC_DIR, 'bin', 'tcc')
        if os.path.exists(tcc_bin):
            os.chmod(tcc_bin, 0o755)

    return find_compiler()


def build_vulpin(progress_callback=None):
    binary_name = 'vulpin.exe' if IS_WIN else 'vulpin'
    binary_path = os.path.join(SRC_DIR, binary_name)

    if os.path.exists(binary_path):
        src_times = [os.path.getmtime(os.path.join(SRC_DIR, s)) for s in C_SOURCES
                     if os.path.exists(os.path.join(SRC_DIR, s))]
        if src_times and os.path.getmtime(binary_path) >= max(src_times):
            if progress_callback:
                progress_callback("binary up to date")
            return binary_path

    makefile = os.path.join(SRC_DIR, 'makefile')
    compiler_path, compiler_type = find_compiler()

    if makefile and os.path.exists(makefile) and compiler_path:
        env = os.environ.copy()
        if compiler_type == 'tcc':
            env['CC'] = compiler_path
        if progress_callback:
            progress_callback("building with makefile...")
        result = subprocess.run(['make', '-C', SRC_DIR], capture_output=True, text=True, env=env)
        if result.returncode == 0 and os.path.exists(binary_path):
            return binary_path

    sources = [os.path.join(SRC_DIR, s) for s in C_SOURCES]
    missing = [s for s in sources if not os.path.exists(s)]
    if missing:
        raise FileNotFoundError(f"Missing source files: {missing}")

    if not compiler_path:
        if progress_callback:
            progress_callback("no compiler found, installing tcc...")
        compiler_path, compiler_type = download_and_install_tcc(progress_callback)
        if not compiler_path:
            raise RuntimeError("Failed to install TCC compiler")

    if progress_callback:
        progress_callback(f"compiling with {compiler_type}...")
    cmd = [compiler_path, '-O2', '-o', binary_path] + sources + ['-lm']
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"{compiler_type} failed:\n{result.stderr}")

    return binary_path


def install_binary(binary_path, install_scripts_dir):
    dest_name = 'vulpin.exe' if IS_WIN else 'vulpin'
    dest = os.path.join(install_scripts_dir, dest_name)
    os.makedirs(install_scripts_dir, exist_ok=True)
    shutil.copy2(binary_path, dest)
    os.chmod(dest, 0o755)
    return dest


# Some commands ->

HELP_TEXT = """
╔══════════════════════════════════════════════╗
║          Vulpin Installation Help            ║
╠══════════════════════════════════════════════╣
║                                              ║
║  GUI Installer (recommended):                ║
║    python setup.py gui                       ║
║                                              ║
║  Console Installer:                          ║
║    python setup.py console                   ║
╚══════════════════════════════════════════════╝
""".strip()


class HelpCommand(Command):
    description = 'Show installation help'
    user_options = []

    def initialize_options(self): pass
    def finalize_options(self): pass

    def run(self):
        print(HELP_TEXT)


class ConsoleInstallCommand(Command):
    description = 'Install Vulpin from console with progress output'
    user_options = []

    def initialize_options(self): pass
    def finalize_options(self): pass

    def run(self):
        def log(msg):
            print(f"[vulpin] {msg}")

        log("building vulpin...")
        binary_path = build_vulpin(progress_callback=log)

        log("running standard install...")
        self.distribution.run_command('build')
        install_cmd = self.distribution.get_command_obj('install')
        install_cmd.ensure_finalized()
        _install.run(install_cmd)

        dest = install_binary(binary_path, install_cmd.install_scripts)
        log(f"installed vulpin binary to: {dest}")
        log("done ✓")


class GuiInstallCommand(Command):
    description = 'Launch modern GUI installer for Vulpin'
    user_options = []

    def initialize_options(self): pass
    def finalize_options(self): pass

    def run(self):
        try:
            import customtkinter as ctk
        except ImportError:
            print("customtkinter not found. Run: pip install customtkinter")
            print("Falling back to console install...\n")
            self.distribution.run_command('console')
            return

        ctk.set_appearance_mode("dark")
        ctk.set_default_color_theme("blue")

        app = ctk.CTk()
        app.title("Vulpin")
        app.geometry("400x440")
        app.resizable(False, False)
        app.configure(fg_color="#0d0d0d")

        app.update_idletasks()
        x = (app.winfo_screenwidth() // 2) - 200
        y = (app.winfo_screenheight() // 2) - 220
        app.geometry(f"+{x}+{y}")

        container = ctk.CTkFrame(app, fg_color="transparent")
        container.pack(expand=True, fill="both", padx=30, pady=30)

        logo = ctk.CTkLabel(
            container, text="vulpin 0.8",
            font=ctk.CTkFont(size=36, weight="bold", family="monospace"),
            text_color="#ffffff"
        )
        logo.pack(pady=(0, 4))

        tagline = ctk.CTkLabel(
            container, text="Wifi requirement, to download some packages.",
            font=ctk.CTkFont(size=12), text_color="#555555"
        )
        tagline.pack(pady=(0, 20))

        options_frame = ctk.CTkFrame(container, fg_color="#141414", corner_radius=10)
        options_frame.pack(pady=(0, 20), fill="x", padx=10)

        docs_var = ctk.BooleanVar(value=True)
        docs_check = ctk.CTkCheckBox(
            options_frame, text="Include documentation",
            font=ctk.CTkFont(size=13), variable=docs_var,
            fg_color="#C35817", hover_color="#C06901",
            checkmark_color="#000000", text_color="#aaaaaa"
        )
        docs_check.pack(anchor="w", padx=15, pady=(12, 6))

        web_var = ctk.BooleanVar(value=True)
        web_check = ctk.CTkCheckBox(
            options_frame, text="Include website files",
            font=ctk.CTkFont(size=13), variable=web_var,
            fg_color="#C35817", hover_color="#C06901",
            checkmark_color="#000000", text_color="#aaaaaa"
        )
        web_check.pack(anchor="w", padx=15, pady=(6, 12))

        progress = ctk.CTkProgressBar(
            container, width=300, height=6, corner_radius=3,
            fg_color="#1a1a1a", progress_color="#C35817"
        )
        progress.pack(pady=(0, 15))
        progress.set(0)

        status = ctk.CTkLabel(
            container, text="",
            font=ctk.CTkFont(size=13), text_color="#666666"
        )
        status.pack(pady=(0, 20))

        btn = ctk.CTkButton(
            container, text="install",
            font=ctk.CTkFont(size=14, weight="bold"),
            width=160, height=42, corner_radius=21,
            fg_color="#C35817", hover_color="#C06901",
            text_color="#000000"
        )

        # Thread ->

        def safe_update(func):
            app.after(0, func)

        def set_status(text, color="#666666"):
            safe_update(lambda: status.configure(text=text, text_color=color))

        def animate_progress(target, duration_ms=300):
            current = progress.get()
            steps = max(1, int(duration_ms / 16))
            delta = (target - current) / steps
            for i in range(steps):
                val = current + delta * (i + 1)
                safe_update(lambda v=val: progress.set(v))
                app.after(16)

        def handle_optional_folders(include_docs, include_web):
            actions = []
            if not include_docs and os.path.isdir(DOCS_DIR):
                disabled = DOCS_DIR + '.disabled'
                if os.path.exists(disabled):
                    shutil.rmtree(disabled)
                os.rename(DOCS_DIR, disabled)
                actions.append("docs excluded")
            if not include_web and os.path.isdir(WEB_DIR):
                disabled = WEB_DIR + '.disabled'
                if os.path.exists(disabled):
                    shutil.rmtree(disabled)
                os.rename(WEB_DIR, disabled)
                actions.append("website excluded")
            return ", ".join(actions) if actions else "all included"

        def do_install():
            safe_update(lambda: btn.configure(state="disabled", text="installing", fg_color="#333333"))
            safe_update(lambda: docs_check.configure(state="disabled"))
            safe_update(lambda: web_check.configure(state="disabled"))

            def worker():
                try:
                    set_status("processing options...")
                    animate_progress(0.05, 200)
                    handle_optional_folders(docs_var.get(), web_var.get())

                    set_status("compiling sources...")
                    animate_progress(0.15, 300)
                    binary_path = build_vulpin(progress_callback=set_status)

                    set_status("installing runtime...")
                    animate_progress(0.5, 400)
                    self.distribution.run_command('build')
                    install_cmd = self.distribution.get_command_obj('install')
                    install_cmd.ensure_finalized()
                    _install.run(install_cmd)

                    set_status("placing binary...")
                    animate_progress(0.8, 300)
                    install_binary(binary_path, install_cmd.install_scripts)

                    set_status("launching...")
                    animate_progress(1.0, 300)

                    if os.path.exists(LAUNCHER_SRC):
                        if IS_WIN:
                            subprocess.Popen([LAUNCHER_SRC], shell=True)
                        else:
                            subprocess.Popen(['bash', LAUNCHER_SRC])

                    set_status("done ✓", "#40d070")
                    safe_update(lambda: btn.configure(
                        text="complete", fg_color="#40d070", hover_color="#30c060"
                    ))

                except Exception as e:
                    set_status(f"failed: {e}", "#e04040")
                    safe_update(lambda: btn.configure(
                        state="normal", text="retry",
                        fg_color="#e04040", hover_color="#d03030"
                    ))
                    safe_update(lambda: docs_check.configure(state="normal"))
                    safe_update(lambda: web_check.configure(state="normal"))

            threading.Thread(target=worker, daemon=True).start()

        btn.configure(command=do_install)
        btn.pack()

        app.mainloop()


# Setup ->

setup(
    name='vulpin',
    version='0.8',
    description='Vulpin programming language',
    packages=[],
    scripts=[],
    cmdclass={
        'gui': GuiInstallCommand,
        'console': ConsoleInstallCommand,
        'help': HelpCommand,
    },
)
