"""Build the GC2Plus .xapk from the original mod's .xapk.

Everything is copied from the original except the setup APK (replaced with the
GC2Plus-signed one) and manifest.json (name, version, total size updated).
The OBBs are copied unchanged; the big song-data OBB is stored uncompressed
(deflate gained nothing on it and is slow).

usage: make_xapk.py <original.xapk> <setup.apk> <out.xapk>
"""
import json
import os
import shutil
import sys
import zipfile

src, setup_apk, dst = sys.argv[1:4]
APK_NAME = "jp.co.taito.groovecoasterzero.apk"
CHUNK = 16 << 20

with zipfile.ZipFile(src) as zin:
    manifest = json.loads(zin.read("manifest.json"))
    manifest["name"] = "Groove 2 setup (GC2Plus)"
    total = os.path.getsize(setup_apk)
    total += sum(i.file_size for i in zin.infolist() if i.filename.startswith("Android/"))
    manifest["total_size"] = total

    tmp = dst + ".part"
    with zipfile.ZipFile(tmp, "w", allowZip64=True) as zout:
        for info in zin.infolist():
            name = info.filename
            if name == "manifest.json":
                zout.writestr("manifest.json", json.dumps(manifest, indent=4), zipfile.ZIP_DEFLATED)
                continue
            if name == APK_NAME:
                zout.write(setup_apk, APK_NAME, zipfile.ZIP_DEFLATED)
                continue
            method = zipfile.ZIP_STORED if info.file_size > (1 << 30) else zipfile.ZIP_DEFLATED
            out_info = zipfile.ZipInfo(name, info.date_time)
            out_info.compress_type = method
            out_info.file_size = info.file_size
            with zin.open(info) as fin, zout.open(out_info, "w", force_zip64=True) as fout:
                shutil.copyfileobj(fin, fout, CHUNK)
            print("copied", name, info.file_size, flush=True)
    os.replace(tmp, dst)
print("wrote", dst, os.path.getsize(dst))
