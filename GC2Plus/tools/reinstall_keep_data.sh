#!/system/bin/sh
# Reinstall the game with a differently-signed APK while keeping its data.
# Run as root: su -c 'sh reinstall_keep_data.sh /data/local/tmp/game.apk'
set -e
PKG=jp.co.taito.groovecoasterzero
APK="$1"
DATA=/data/user/0/$PKG
OBB=/data/media/0/Android/obb/$PKG
EXT=/data/media/0/Android/data/$PKG
BK=/data/local/tmp/gc2plus_backup

[ -f "$APK" ] || { echo "missing apk $APK"; exit 1; }
[ -e "$BK" ] && { echo "$BK already exists, refusing to overwrite"; exit 1; }
mkdir -p $BK

am force-stop $PKG
# Same-partition renames: instant, nothing copied.
mv $DATA $BK/data
[ -d $OBB ] && mv $OBB $BK/obb
[ -d $EXT ] && mv $EXT $BK/ext
echo "data moved aside"

pm uninstall $PKG
pm install -r "$APK"
echo "installed"

UID_NEW=$(stat -c %u /data/user/0/$PKG)
CTX=$(ls -dZ /data/user/0/$PKG | cut -d' ' -f1)
rm -rf /data/user/0/$PKG
mv $BK/data /data/user/0/$PKG
chown -R $UID_NEW:$UID_NEW /data/user/0/$PKG
# cache dirs belong to the app's cache group (uid + 10000)
for d in cache code_cache; do
  [ -d /data/user/0/$PKG/$d ] && chown -R $UID_NEW:$((UID_NEW + 10000)) /data/user/0/$PKG/$d
done
# restorecon can't recover the per-app MLS categories; reuse the fresh install's label.
chcon -R "$CTX" /data/user/0/$PKG

if [ -d $BK/obb ]; then
  mkdir -p /data/media/0/Android/obb
  rm -rf $OBB
  mv $BK/obb $OBB
  chown -R $UID_NEW:ext_obb_rw $OBB 2>/dev/null || chown -R $UID_NEW $OBB
fi
if [ -d $BK/ext ]; then
  rm -rf $EXT
  mv $BK/ext $EXT
  chown -R $UID_NEW:ext_data_rw $EXT 2>/dev/null || chown -R $UID_NEW $EXT
fi
restorecon -R /data/media/0/Android/obb/$PKG /data/media/0/Android/data/$PKG 2>/dev/null || true
rmdir $BK
echo "done uid=$UID_NEW"
