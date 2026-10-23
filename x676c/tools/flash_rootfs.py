#!/usr/bin/env python3
import gzip
import os
import subprocess
import sys
import time

IMG = r"\\wsl$\Ubuntu\home\captain\ios-x676c\ios16-root.img"
ADB = os.path.expandvars(r"%LOCALAPPDATA%\platform-tools-dl\platform-tools\adb.exe")

def main():
    if not os.path.exists(IMG):
        print(f"Error: image not found at {IMG}", file=sys.stderr)
        sys.exit(1)

    total_size = os.path.getsize(IMG)
    print(f"Source image: {IMG} ({total_size / (1024**3):.2f} GiB)")
    print(f"Target: /dev/block/by-name/iosroot via ADB ({ADB})")

    # Start adb receiver with zcat
    cmd = [ADB, "exec-in", "su -c 'zcat > /dev/block/by-name/iosroot'"]
    print("Launching stream: " + " ".join(cmd))
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE)

    chunk_size = 16 * 1024 * 1024 # 16 MiB chunks
    written = 0
    start_time = time.time()
    last_print = start_time

    try:
        with open(IMG, "rb") as f:
            with gzip.GzipFile(fileobj=proc.stdin, mode="wb", compresslevel=1) as gz:
                while True:
                    data = f.read(chunk_size)
                    if not data:
                        break
                    gz.write(data)
                    written += len(data)
                    now = time.time()
                    if now - last_print >= 2.0 or written == total_size:
                        elapsed = now - start_time
                        speed = (written / (1024 * 1024)) / (elapsed if elapsed > 0 else 1)
                        pct = (written / total_size) * 100.0
                        print(f"\rProgress: {pct:5.1f}% ({written // (1024*1024)}/{total_size // (1024*1024)} MiB) @ {speed:.1f} MiB/s...", end="", flush=True)
                        last_print = now
        print("\nFlushing and syncing storage on device...")
        proc.stdin.close()
        proc.wait()
        
        # Sync on device
        subprocess.run([ADB, "shell", "su -c 'sync'"], check=True)
        elapsed = time.time() - start_time
        print(f"SUCCESS: Flashed {total_size // (1024**2)} MiB in {elapsed:.1f}s ({total_size / (1024**2) / elapsed:.1f} MiB/s)!")
    except Exception as e:
        print(f"\nError during streaming: {e}", file=sys.stderr)
        proc.kill()
        sys.exit(1)

if __name__ == "__main__":
    main()
