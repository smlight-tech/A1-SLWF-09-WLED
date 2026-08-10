Import('env')
import os
import shutil
import gzip
import json

OUTPUT_DIR = "build_output{}".format(os.path.sep)
#OUTPUT_DIR = os.path.join("build_output")

def _get_cpp_define_value(env, define):
    define_list = [item[-1] for item in env["CPPDEFINES"] if item[0] == define]

    if define_list:
        return define_list[0]

    return None

def _create_dirs(dirs=["map", "release", "firmware"]):
    for d in dirs:
        os.makedirs(os.path.join(OUTPUT_DIR, d), exist_ok=True)

def create_release(source):
    release_name_def = _get_cpp_define_value(env, "WLED_RELEASE_NAME")
    if release_name_def:
        release_name = release_name_def.replace("\\\"", "")
        with open("package.json", "r") as package:
            version = json.load(package)["version"]        
        release_file = os.path.join(OUTPUT_DIR, "release", f"WLED_{version}_{release_name}.bin")
        release_gz_file = release_file + ".gz"
        print(f"Copying {source} to {release_file}")
        shutil.copy(source, release_file)
        bin_gzip(release_file, release_gz_file)
    else:
        variant = env["PIOENV"]
        bin_file = "{}firmware{}{}.bin".format(OUTPUT_DIR, os.path.sep, variant)
        print(f"Copying {source} to {bin_file}")
        shutil.copy(source, bin_file)

def bin_rename_copy(source, target, env):
    _create_dirs()
    variant = env["PIOENV"]
    builddir = os.path.join(env["PROJECT_BUILD_DIR"],  variant)
    source_map = os.path.join(builddir, env["PROGNAME"] + ".map")

    # create string with location and file names based on variant
    map_file = "{}map{}{}.map".format(OUTPUT_DIR, os.path.sep, variant)

    create_release(str(target[0]))

    # copy firmware.map to map/<variant>.map
    if os.path.isfile("firmware.map"):
        print("Found linker mapfile firmware.map")
        shutil.copy("firmware.map", map_file)
    if os.path.isfile(source_map):
        print(f"Found linker mapfile {source_map}")
        shutil.copy(source_map, map_file)

def bin_gzip(source, target):
    # only create gzip for esp8266
    if not env["PIOPLATFORM"] == "espressif8266":
        return
    
    print(f"Creating gzip file {target} from {source}")
    with open(source,"rb") as fp:
        with gzip.open(target, "wb", compresslevel = 9) as f:
            shutil.copyfileobj(fp, f)

def merge_bin(source, target, env):
    # build a single full-flash image (bootloader + partitions + boot_app0 + app)
    # that can be flashed at offset 0x0 with esptool / web flasher. esp32 only.
    if env["PIOPLATFORM"] != "espressif32":
        return

    _create_dirs()
    variant = env["PIOENV"]
    release_name_def = _get_cpp_define_value(env, "WLED_RELEASE_NAME")
    version = _get_cpp_define_value(env, "WLED_VERSION")

    if release_name_def:
        release_name = release_name_def.replace("\\\"", "")
        with open("package.json", "r") as package:
            version = json.load(package)["version"]        
            full_file = os.path.join(OUTPUT_DIR, "release", f"WLED_{version}_{release_name}_full.bin")
    else:
        full_file = os.path.join(OUTPUT_DIR, "firmware", f"{variant}_full.bin")

    board = env.BoardConfig()
    mcu = board.get("build.mcu", "esp32")
    flash_size = board.get("upload.flash_size", "4MB")
    flash_mode = "dio"
    # build.f_flash like "80000000L" -> "80m"
    f_flash = str(board.get("build.f_flash", "40000000L"))
    flash_freq = f_flash.replace("000000L", "m").replace("000000", "m").replace("L", "")

    # bootloader + partitions (+ boot_app0) with their offsets, as PlatformIO computed them
    images = []
    for offset, image in env["FLASH_EXTRA_IMAGES"]:
        images += [str(offset), env.subst(image)]
    # the application image goes at the app offset
    images += [env.subst("$ESP32_APP_OFFSET"), str(target[0])]

    cmd = [
        "$PYTHONEXE", "$OBJCOPY",
        "--chip", mcu, "merge_bin",
        "-o", full_file,
        "--flash_mode", flash_mode,
        "--flash_freq", flash_freq,
        "--flash_size", flash_size,
    ] + images

    print(f"Building full flash image {full_file}")
    env.Execute(env.VerboseAction(env.subst(" ".join(cmd)),
                                  f"Merging full flash image {full_file}"))

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", bin_rename_copy)
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_bin)
