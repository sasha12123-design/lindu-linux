#!/usr/bin/env python3
# lindu_install_report.py — сбор полного отчёта об установке lindu linux
# и авто-сохранение на внешний носитель (флешку/SD), чтобы его можно было
# принести на другой компьютер для разбора.
#
# Зачем: установщик работает в live-среде, где /tmp — оперативная память.
# Лог, сохранённый только в /tmp, исчезает при перезагрузке, поэтому при
# ошибке отчёт дублируется на любой найденный раздел с файловой системой.
#
# Использование:
#   lindu-install-report --reason "текст ошибки" [--target /dev/sda] [--mount /mnt]
#   lindu-install-report --flush-only            # быстрый снимок текущего лога
#
# Модуль также импортируется установщиком (см. lindu-install-gtk):
#   import lindu_install_report as rep
#   rep.flush_live(...)   # периодически дублировать живой лог на носители
#   rep.dump(...)         # полный отчёт при ошибке

import argparse
import datetime
import json
import os
import subprocess
import sys
import time
import traceback

LIVE_LOG = "/tmp/lindu-install.log"
MOUNT_DEFAULT = "/mnt"
STATE_DIR = "/run/lindu-report"
MAX_LINES = 3000

# ФС, на которых имеет смысл искать место для отчёта.
WRITABLE_FSTYPES = (
    "vfat", "fat", "fat32", "exfat", "ntfs", "ntfs3", "ntfs-3g",
    "fuseblk", "ext2", "ext3", "ext4",
)

MOUNT_OPTS = {
    "vfat": ["-t", "vfat", "-o", "rw,uid=0,gid=0,umask=000"],
    "fat": ["-t", "vfat", "-o", "rw,uid=0,gid=0,umask=000"],
    "fat32": ["-t", "vfat", "-o", "rw,uid=0,gid=0,umask=000"],
    "exfat": ["-t", "exfat", "-o", "rw,uid=0,gid=0"],
    "ntfs3": ["-t", "ntfs3", "-o", "rw,uid=0,gid=0"],
    "ntfs": ["-t", "ntfs3", "-o", "rw,uid=0,gid=0"],
    "ntfs-3g": ["-t", "ntfs-3g", "-o", "rw,uid=0,gid=0"],
    "fuseblk": ["-t", "fuseblk", "-o", "rw,uid=0,gid=0"],
    "ext4": ["-t", "ext4", "-o", "rw"],
    "ext3": ["-t", "ext3", "-o", "rw"],
    "ext2": ["-t", "ext2", "-o", "rw"],
}


# --------------------------------------------------------------- утилиты ---

def sh(cmd, timeout=30):
    """Выполнить команду и вернуть вывод (stdout+stderr) строкой."""
    try:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           timeout=timeout, errors="replace")
        return (p.stdout or "").rstrip()
    except Exception as e:
        return "!! %s: %s" % (type(e).__name__, e)


def tail_file(path, lines=MAX_LINES):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            data = f.read()
    except Exception as e:
        return "!! нет файла %s (%s)" % (path, e)
    if not data.strip():
        return "(пусто)"
    parts = data.splitlines()
    if len(parts) > lines:
        parts = ["... [%d более ранних строк пропущено] ..." % (len(parts) - lines)] + parts[-lines:]
    return "\n".join(parts)


def section(title, body):
    return "\n\n===== %s =====\n%s\n" % (title, body if body else "(пусто)")


def is_mountpoint(path):
    try:
        with open("/proc/mounts", "r") as f:
            for line in f:
                parts = line.split()
                if len(parts) > 1 and parts[1] == path:
                    return True
    except Exception:
        pass
    return False


def _writable(path):
    probe = os.path.join(path, ".lindu-write-test")
    try:
        with open(probe, "w") as f:
            f.write("ok")
        os.remove(probe)
        return True
    except Exception:
        return False


def ensure_rw(dev, mnt):
    """Если носитель уже смонтирован, но только на чтение — пробуем rw."""
    if _writable(mnt):
        return True
    sh(["mount", "-o", "remount,rw", dev, mnt], timeout=30)
    return _writable(mnt)


def _mountpoints_of(node):
    mp = node.get("mountpoints")
    if isinstance(mp, str):
        return [mp] if mp else []
    return [m for m in (mp or []) if m]


def _flatten(nodes):
    for n in nodes or []:
        yield n
        for c in _flatten(n.get("children")):
            yield c


# --------------------------------------------------------- поиск носителей ---

def _target_names(target):
    if not target:
        return set()
    t = target.split("/")[-1]
    return {t}


def find_media(target=None, mount=MOUNT_DEFAULT):
    """Разделы с ФС, пригодные для записи отчёта (кроме разделов целевого диска)."""
    raw = sh(["lsblk", "-J", "-b", "-o",
              "NAME,PATH,TYPE,FSTYPE,LABEL,RM,SIZE,MOUNTPOINTS,PKNAME"], timeout=20)
    try:
        tree = json.loads(raw)
    except Exception:
        return []
    targets = _target_names(target)
    out = []
    for n in _flatten(tree.get("blockdevices")):
        # раздел или «суперфлоппи» (диск с ФС без таблицы разделов)
        if n.get("type") not in ("part", "disk"):
            continue
        name = n.get("name") or ""
        if name.startswith(("loop", "ram", "zram", "sr", "fd", "dm-")):
            continue
        fstype = (n.get("fstype") or "").lower()
        if fstype not in WRITABLE_FSTYPES:
            continue
        pk = n.get("pkname") or ""
        if name in targets or pk in targets or (targets and name.split("p")[0] in targets):
            continue
        mps = _mountpoints_of(n)
        # пропускаем носитель live-образа (read-only) и всё внутри цели
        if any(m.startswith("/run/archiso") for m in mps):
            continue
        if mount and any(m == mount or m.startswith(mount.rstrip("/") + "/") for m in mps):
            continue
        path = n.get("path") or ("/dev/" + name)
        out.append({
            "dev": path,
            "fstype": fstype,
            "label": n.get("label") or "",
            "rm": str(n.get("rm")) == "1",
            "size": int(n.get("size") or 0),
            "mountpoints": mps,
        })
    # съёмные — первыми, потом крупные
    out.sort(key=lambda c: (0 if c["rm"] else 1, -c["size"]))
    return out


def mount_media(cand):
    """Смонтировать раздел rw во временный каталог; вернуть путь или None."""
    base = os.path.join(STATE_DIR, "media")
    try:
        os.makedirs(base, exist_ok=True)
    except Exception:
        return None
    tag = cand["dev"].split("/")[-1].replace("/", "_")
    mnt = os.path.join(base, tag)
    fs = cand["fstype"]
    attempts = []
    if fs in MOUNT_OPTS:
        attempts.append(MOUNT_OPTS[fs])
    attempts.append(["-o", "rw"])
    attempts.append([])
    for extra in attempts:
        cmd = ["mount"] + list(extra) + [cand["dev"], mnt]
        try:
            os.makedirs(mnt, exist_ok=True)
        except Exception:
            return None
        sh(cmd, timeout=30)
        if not is_mountpoint(mnt):
            continue
        if ensure_rw(cand["dev"], mnt):
            return mnt
        sh(["umount", "-l", mnt], timeout=20)
    return None


def media_dirs(target=None, mount=MOUNT_DEFAULT, use_cache=True):
    """Список смонтированных rw-каталогов-носителей (с кэшем в /run)."""
    cache_path = os.path.join(STATE_DIR, "dirs.json")
    if use_cache:
        try:
            with open(cache_path, "r") as f:
                cache = json.load(f)
            if cache.get("target", "") == (target or "") and \
               time.time() - cache.get("ts", 0) < 60:
                alive = []
                seen = set()
                for d in cache.get("dirs", []):
                    if is_mountpoint(d) and _writable(d):
                        st = os.stat(d).st_dev
                        if st not in seen:
                            seen.add(st)
                            alive.append(d)
                if alive:
                    return alive
        except Exception:
            pass
    dirs, seen = [], set()

    def _add(d):
        # один и тот же носитель может быть виден и как диск, и как раздел
        try:
            st = os.stat(d).st_dev
        except OSError:
            return False
        if st in seen:
            return False
        seen.add(st)
        dirs.append(d)
        return True

    for cand in find_media(target, mount):
        mps = [m for m in cand["mountpoints"] if is_mountpoint(m)]
        if mps and ensure_rw(cand["dev"], mps[0]):
            _add(mps[0])
            continue
        d = mount_media(cand)
        if d:
            _add(d)
            if len(dirs) >= 3:
                break
    try:
        os.makedirs(STATE_DIR, exist_ok=True)
        with open(cache_path, "w") as f:
            json.dump({"ts": time.time(), "target": target or "", "dirs": dirs}, f)
    except Exception:
        pass
    return dirs


def fallback_dirs(mount=MOUNT_DEFAULT):
    """Куда ещё положить копию отчёта, даже если внешних носителей нет."""
    out = ["/tmp", "/root"]
    for sub in ("boot", "var/log", "root"):
        p = os.path.join(mount, sub)
        try:
            if os.path.isdir(p) and is_mountpoint(p):
                out.append(p)
        except Exception:
            pass
    return out


# ------------------------------------------------------------ сбор отчёта ---

def build_report(reason="", logpath=LIVE_LOG, target=None, mount=MOUNT_DEFAULT,
                 tb="", setup=None, stage="", extra=""):
    now = datetime.datetime.now()
    head = []
    head.append("lindu linux — ОТЧЁТ ОБ ОШИБКЕ УСТАНОВКИ")
    head.append("=" * 60)
    head.append("создан:            %s" % now.strftime("%Y-%m-%d %H:%M:%S"))
    head.append("причина:           %s" % (reason or "не указана"))
    if stage:
        head.append("этап:              %s" % stage)
    head.append("диск-цель:         %s" % (target or "?"))
    head.append("точка монтирования: %s" % mount)
    if setup:
        head.append("параметры:         %s" % json.dumps(setup, ensure_ascii=False))
    head.append("ядро:              %s" % sh(["uname", "-a"]))
    head.append("archiso/live:      %s" % (
        sh(["grep", "-m1", "-oE", "archisolabel=[^ ]*|archisosearchuuid=[^ ]*",
            "/proc/cmdline"], timeout=10) or "?"))
    head.append("дата live-образа:  %s" % (
        sh(["stat", "-c", "%y", "/run/archiso/bootmnt"], timeout=10) or "?"))
    head.append("")
    head.append("Отчёт можно просто открыть текстовым редактором.")
    parts = ["\n".join(head)]

    if tb:
        parts.append(section("ТРАССИРОВКА ОШИБКИ (Python)", tb))

    parts.append(section("ЖУРНАЛ УСТАНОВКИ (полный, как показывал в окне)",
                         tail_file(logpath)))
    parts.append(section("СОСТОЯНИЕ ЦЕЛЕВОГО ДИСКА (lsblk -f)",
                         sh(["lsblk", "-f"], timeout=20)))
    parts.append(section("РАЗМЕТКА (parted print)",
                         sh(["bash", "-c",
                             "for d in %s; do [ -b $d ] && parted -s $d print free; done"
                             % (target or "")], timeout=30)))
    parts.append(section("ТОЧКИ МОНТИРОВАНИЯ (findmnt)",
                         sh(["findmnt", "-rno", "SOURCE,TARGET,FSTYPE,OPTIONS"], timeout=20)))
    parts.append(section("МЕСТО НА ДИСКАХ (df -hT)",
                         sh(["df", "-hT"], timeout=20)))
    parts.append(section("ЖУРНАЛ pacman (live, хвост)",
                         tail_file("/var/log/pacman.log", 400)))
    parts.append(section("ЖУРНАЛ pacman (в цели, хвост)",
                         tail_file(os.path.join(mount, "var/log/pacman.log"), 400)))
    parts.append(section("fstab цели",
                         tail_file(os.path.join(mount, "etc/fstab"), 100)))
    parts.append(section("grub.cfg цели (если создан)",
                         tail_file(os.path.join(mount, "boot/grub/grub.cfg"), 300)))
    parts.append(section("СОДЕРЖИМОЕ ЦЕЛИ (верхний уровень)",
                         sh(["bash", "-c", "ls -la %s 2>&1 | head -40" % mount], timeout=20)))
    parts.append(section("dmesg (хвост)",
                         sh(["bash", "-c", "dmesg 2>/dev/null | tail -200 || journalctl -k -b --no-pager | tail -200"], timeout=30)))
    parts.append(section("systemd journal (хвост)",
                         sh(["bash", "-c", "journalctl -b --no-pager 2>/dev/null | tail -300"], timeout=40)))
    parts.append(section("ПОСЛЕДНИЕ ЛОГИ И СЕРВИСЫ ЦЕЛИ",
                         sh(["bash", "-c",
                             "systemctl --no-pager --root=%s list-units --failed 2>&1 | head -20; "
                             "ls -la %s/etc/systemd/system/getty.target.wants/ 2>&1 | head -20"
                             % (mount, mount)], timeout=30)))
    parts.append(section("СЕТЬ (нужна для pacstrap)",
                         sh(["bash", "-c",
                             "ip -br a 2>/dev/null | head -10; "
                             "getent hosts geo.mirror.pkgbuild.com || echo 'DNS: не резолвится'; "
                             "curl -m 8 -sSI https://geo.mirror.pkgbuild.com/ 2>&1 | head -3"], timeout=30)))
    parts.append(section("НАЛИЧИЕ ИНСТРУМЕНТОВ",
                         sh(["bash", "-c",
                             "for t in pacstrap arch-chroot genfstab parted partprobe "
                             "mkfs.fat mkfs.ext4 grub-install; do printf '%-14s %s\\n' $t "
                             "$(command -v $t || echo НЕТ); done"], timeout=20)))
    if extra:
        parts.append(section("ДОПОЛНИТЕЛЬНО", extra))
    parts.append("\n\n===== КОНЕЦ ОТЧЁТА =====\n")
    return "\n".join(parts)


def write_files(text, dirs, stamp=None, fixed=None):
    """Разложить текст по носителям. fixed=имя без метки времени (живой снимок)."""
    if fixed:
        fname = fixed
    else:
        stamp = stamp or datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        fname = "lindu-install-error-%s.txt" % stamp
    written, failed = [], []
    for d in dirs:
        p = os.path.join(d, fname)
        try:
            with open(p, "w", encoding="utf-8", errors="replace") as f:
                f.write(text)
                f.flush()
                os.fsync(f.fileno())
            written.append(p)
        except Exception as e:
            failed.append("%s (%s)" % (p, e))
    # понятное имя без метки времени — чтобы отчёт легко найти на флешке
    if not fixed:
        for d in dirs:
            try:
                with open(os.path.join(d, "INSTALL-ERROR.txt"), "w",
                          encoding="utf-8", errors="replace") as f:
                    f.write(text)
                    f.flush()
                    os.fsync(f.fileno())
            except Exception:
                pass
    return fname, written, failed


def live_snapshot(logpath=LIVE_LOG, target=None, mount=MOUNT_DEFAULT, dirs=None,
                  stage="", setup=None):
    """Лёгкий снимок текущего журнала на носители (для случая зависания).

    Пишется один и тот же файл (перезаписывается), намеренно без dmesg/journal,
    чтобы обновление было быстрым и не съедало флешку.
    """
    dirs = dirs if dirs is not None else media_dirs(target, mount)
    if not dirs:
        return [], []
    now = datetime.datetime.now()
    head = [
        "lindu linux — ТЕКУЩИЙ ЖУРНАЛ УСТАНОВКИ (установка ещё шла или зависла)",
        "=" * 60,
        "снимок:            %s" % now.strftime("%Y-%m-%d %H:%M:%S"),
        "этап:              %s" % (stage or "?"),
        "диск-цель:         %s" % (target or "?"),
        "точка монтирования: %s" % mount,
    ]
    if setup:
        head.append("параметры:         %s" % json.dumps(setup, ensure_ascii=False))
    head.append("")
    parts = ["\n".join(head),
             section("ЖУРНАЛ УСТАНОВКИ (полный текущий)", tail_file(logpath)),
             section("pacman.log (хвост, live)", tail_file("/var/log/pacman.log", 150)),
             section("pacman.log (хвост, цель)",
                     tail_file(os.path.join(mount, "var/log/pacman.log"), 150)),
             section("dmesg (хвост 60)",
                     sh(["bash", "-c", "dmesg 2>/dev/null | tail -60"], timeout=20)),
             "\n===== КОНЕЦ СНИМКА =====\n"]
    text = "\n".join(parts)
    fname, written, failed = write_files(text, dirs, fixed="lindu-install-live.txt")
    return written, failed


# старое имя — используется фоновым «насосом» журнала в lindu-install
def flush_live(logpath=LIVE_LOG, target=None, mount=MOUNT_DEFAULT, dirs=None,
               stamp=None, stage="", setup=None):
    return live_snapshot(logpath, target, mount, dirs, stage, setup)


def dump(reason="", logpath=LIVE_LOG, target=None, mount=MOUNT_DEFAULT,
         tb="", setup=None, stage="", extra=""):
    """Полный отчёт об ошибке: собираем, ищем носители, пишем, возвращаем пути."""
    if not tb:
        et, ev, eet = sys.exc_info()
        tb = "".join(traceback.format_exception(et, ev, eet)) if et else "(трассировка недоступна)"
    text = build_report(reason=reason, logpath=logpath, target=target,
                        mount=mount, tb=tb, setup=setup, stage=stage, extra=extra)
    dirs = media_dirs(target, mount, use_cache=False) + fallback_dirs(mount)
    seen, uniq = set(), []
    for d in dirs:
        if d not in seen:
            seen.add(d)
            uniq.append(d)
    fname, written, failed = write_files(text, uniq)
    return {"file": fname, "written": written, "failed": failed, "dirs": uniq, "text": text}


# ------------------------------------------------------------------- CLI ---

def main():
    ap = argparse.ArgumentParser(description="Отчёт об ошибке установки lindu linux")
    ap.add_argument("--reason", default="")
    ap.add_argument("--logfile", default=LIVE_LOG)
    ap.add_argument("--target", default="")
    ap.add_argument("--mount", default=MOUNT_DEFAULT)
    ap.add_argument("--stage", default="")
    ap.add_argument("--setup", default="", help="JSON с параметрами установки")
    ap.add_argument("--traceback", default="")
    ap.add_argument("--flush-only", action="store_true",
                    help="только снимок текущего лога (для зависаний)")
    ap.add_argument("--list-media", action="store_true", help="показать найденные носители")
    args = ap.parse_args()

    if args.list_media:
        for c in find_media(args.target or None, args.mount):
            print("  %-16s %-7s removable=%-5s %s" %
                  (c["dev"], c["fstype"], c["rm"], c["label"]))
        return 0

    setup = None
    if args.setup:
        try:
            setup = json.loads(args.setup)
        except Exception:
            setup = {"raw": args.setup}

    if args.flush_only:
        written, failed = live_snapshot(args.logfile, args.target or None, args.mount,
                                        stage=args.stage, setup=setup)
        for p in written:
            print("снимок сохранён: %s" % p)
        for p in failed:
            print("ОШИБКА записи: %s" % p)
        if not written:
            print("(!) внешние носители не найдены — снимок только в %s" % args.logfile)
        return 0

    res = dump(reason=args.reason, logpath=args.logfile, target=args.target or None,
               mount=args.mount, tb=args.traceback, setup=setup, stage=args.stage)
    print("причина: %s" % (args.reason or "не указана"))
    print("файл:    %s" % res["file"])
    for p in res["written"]:
        print("сохранено: %s" % p)
    for p in res["failed"]:
        print("ОШИБКА записи: %s" % p)
    if not res["written"]:
        print("!!! НИ ОДНОГО носителя не найдено — отчёт остался в памяти")
        print("    вставьте флешку с FAT32 и повторите, либо заберите /tmp/%s" % res["file"])
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())