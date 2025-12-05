Import("env")
import os
import re
import time

def fix_cmake_before_build(source, target, env):
    """Fix IDF_VER quotes in build_properties.temp.cmake before CMake runs"""
    build_dir = env.subst("$BUILD_DIR")
    props_file = os.path.join(build_dir, "build_properties.temp.cmake")

    # Wait a moment for file to be created
    max_wait = 5
    waited = 0
    while not os.path.exists(props_file) and waited < max_wait:
        time.sleep(0.1)
        waited += 0.1

    if os.path.exists(props_file):
        with open(props_file, 'r') as f:
            content = f.read()

        # Fix: DIDF_VER="4.4.2"" -> DIDF_VER=\"4.4.2\"
        original = content
        content = re.sub(r'-DIDF_VER="([^"]+)""', r'-DIDF_VER=\\"\\1\\"', content)

        if content != original:
            with open(props_file, 'w') as f:
                f.write(content)
            print("✓ Fixed IDF_VER quotes in build_properties.temp.cmake")

    return None

# Hook very early in the build process
env.AddPreAction("$BUILD_DIR/CMakeCache.txt", fix_cmake_before_build)
env.AddPreAction("$BUILD_DIR/build_properties.temp.cmake", fix_cmake_before_build)
