Import("env")

import os
import shutil


if env.subst("$PIOENV") in ("esp32-s3-4mb", "esp32-s3-4mb-psram"):
    files = (
        "index.html",
        "app.js",
        "style.css",
        "dbc/gwm_haval_h6_phev_2024.dbc",
        "dbc/hongqi_hs5.dbc",
        "dbc/mg.dbc",
        "dbc/luxgen_s5_2015.dbc",
        "dbc/psa_aee2010_r3.dbc",
        "dbc/hyundai_2015_ccan.dbc",
        "dbc/hyundai_2015_mcan.dbc",
        "dbc/hyundai_i30_2014.dbc",
        "dbc/nissan_xterra_2011.dbc",
        "dbc/toyota_2017_ref_pt.dbc",
    )
    source = os.path.join(env.subst("$PROJECT_DIR"), "data")
    staged = os.path.join(env.subst("$BUILD_DIR"), "cartouch-4mb-data")
    if os.path.isdir(staged):
        shutil.rmtree(staged)
    for relative_path in files:
        src = os.path.join(source, relative_path)
        dst = os.path.join(staged, relative_path)
        if not os.path.isfile(src):
            raise RuntimeError("Required 4 MB filesystem asset is missing: " + src)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(src, dst)
    env.Replace(PROJECT_DATA_DIR=staged)