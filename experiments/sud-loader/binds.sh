# The binds login.nix (modules/environment/login/login.nix) gives proot, as
# launcher options, in the same order and with the same conditions; sourced
# by the native scripts:  $L $B TARGET argv0 ...
# Store paths are this device's (see the proot command line of a session).
usr=/data/data/us.lassul.droidnix/files/usr
FPS=$usr/nix/store/9fndcbb48kqi8gcw89136ljvv59pzyvd-fakeProcStat
FPU=$usr/nix/store/a5q6ifzqz6gmxnqxrvk643gj9sawbsm6-fakeProcUptime
B="-b $usr/nix:/nix -b $usr/bin:/bin! -b $usr/etc:/etc! -b $usr/tmp:/tmp -b $usr/usr:/usr -b $usr/dev/shm:/dev/shm"
if [ ! -r /proc/stat ] && [ -e $FPS ]; then B="$B -b $FPS:/proc/stat"; fi
if [ ! -r /proc/uptime ] && [ -e $FPU ]; then B="$B -b $FPU:/proc/uptime"; fi
B="$B -b /:/android"
