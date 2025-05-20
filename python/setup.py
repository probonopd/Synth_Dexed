from setuptools import setup, Extension
import pybind11
import os
import sys

# Collect all relevant source files for Dexed
src_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "../src"))
sources = [
    os.path.join(src_dir, "dexed_pybind.cpp"),
    os.path.join(src_dir, "dexed.cpp"),
    os.path.join(src_dir, "fm_op_kernel.cpp"),
    os.path.join(src_dir, "env.cpp"),
    os.path.join(src_dir, "pitchenv.cpp"),
    os.path.join(src_dir, "dx7note.cpp"),
    os.path.join(src_dir, "lfo.cpp"),
    os.path.join(src_dir, "EngineMsfa.cpp"),
    os.path.join(src_dir, "EngineMkI.cpp"),
    os.path.join(src_dir, "EngineOpl.cpp"),
    os.path.join(src_dir, "fm_core.cpp"),
    os.path.join(src_dir, "exp2.cpp"),
    os.path.join(src_dir, "freqlut.cpp"),
    os.path.join(src_dir, "sin.cpp"),
    os.path.join(src_dir, "porta.cpp"),
    os.path.join(src_dir, "synth_dexed.cpp"),
    # Add any other .cpp files your Dexed implementation requires
]

compile_args = []
if sys.platform == "win32":
    compile_args.append("/std:c++17")
else:
    compile_args.append("-std=c++17")

libraries = (
    ["winmm"] if sys.platform == "win32"
    else ["asound"] if sys.platform.startswith("linux")
    else []
)
extra_link_args = []
if sys.platform == "darwin":
    extra_link_args = [
        "-framework", "CoreAudio",
        "-framework", "AudioUnit",
        "-framework", "CoreMIDI"
    ]

ext_modules = [
    Extension(
        "dexed_py",
        sources,
        include_dirs=[
            pybind11.get_include(),
            src_dir
        ],
        language="c++",
        extra_compile_args=compile_args,
        libraries=libraries,
        extra_link_args=extra_link_args,
        # py_limited_api=True,
        # define_macros=[("Py_LIMITED_API", "0x03020000")],
    ),
]

setup(
    name="dexed_py",
    version="0.1",
    author="Your Name",
    description="Python bindings for Dexed synth",
    ext_modules=ext_modules,
    install_requires=["pybind11"],
    zip_safe=False,
    py_modules=[],
)
