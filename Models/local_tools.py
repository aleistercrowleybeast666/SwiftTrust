"""Portable tool lookup; no SDK or toolchain is copied into the repository."""
import os,shutil
from pathlib import Path
def executable(env,name):
 value=os.environ.get(env) or shutil.which(name)
 if not value:raise RuntimeError('Set '+env+' or add '+name+' to PATH')
 return Path(value)
def sdk_root():return Path(os.environ['SDK_ROOT']).resolve()
def arm_root():
 if os.environ.get('ARM_GCC_ROOT'):return Path(os.environ['ARM_GCC_ROOT']).resolve()
 return executable('ARM_CC','arm-none-eabi-gcc').resolve().parents[1]
