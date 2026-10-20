"""Run the installed EIDE native builder on both actual project source lists.
All binary/cache output is temporary and removed. Does not flash or open hardware.
Requires SDK_ROOT and ARM_GCC_ROOT; EIDE_EXTENSION may override extension discovery.
"""
import datetime,json,os,subprocess,tempfile,copy,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
 sdk=Path(os.environ['SDK_ROOT']).resolve();tool=Path(os.environ['ARM_GCC_ROOT']).resolve()
 extensions=sorted((Path.home()/'.vscode/extensions').glob('cl.eide-*'))
 ext=Path(os.environ.get('EIDE_EXTENSION',str(extensions[-1]) if extensions else '')).resolve()
 builder=ext/'res/tools/win32/unify_builder/unify_builder.exe'
 reports=[datetime.datetime.now(datetime.timezone.utc).isoformat(),
  'Actual EIDE unify_builder; component LINK_CHECK targets only, NOT operational firmware.',
  'No hardware access. All outputs in automatically removed temporary directories.']
 with tempfile.TemporaryDirectory(prefix='swift-eide-') as d:
  temp=Path(d)
  for board,cpu in [('Acquisition','cortex-m4'),('Controller','cortex-m0+')]:
   root=ROOT/board;project=json.loads((root/'.eide/eide.yml').read_text());target=project['targets']['LinkCheck']
   def expand(s):return s.replace('${SDK_ROOT}',sdk.as_posix())
   options=copy.deepcopy(target['toolchainConfigMap']['GCC']['options'])
   options['global'].update({'toolPrefix':'arm-none-eabi-','microcontroller-cpu':cpu,'microcontroller-fpu':cpu,'microcontroller-float':cpu,'$arch-extensions':'','$clang-arch-extensions':'','$armlink-arch-extensions':''})
   options['linker']['link-scatter']=[(root/'board/memory.ld').as_posix()]
   options['linker']['$toolName']='gcc'
   out=temp/board;out.mkdir();sources=[expand(f['path']) for f in project['virtualFolder']['files']]
   assert all((root/s).is_file() for s in sources)
   params={'name':project['name'],'target':'LinkCheck','toolchain':'GCC','toolchainLocation':str(tool),
    'toolchainCfgFile':str(ext/'res/data/models/arm.gcc.model.json'),'buildMode':'fast|multhread','threadNum':2,
    'rootDir':str(root),'dumpPath':str(out),'outDir':str(out),
    'incDirs':[expand(p) for p in target['cppPreprocessAttrs']['incList']],
    'libDirs':[],'defines':target['cppPreprocessAttrs']['defineList'],'sourceList':sources,
    'options':options,'env':{'ProjectRoot':str(root),'OutDir':str(out),'SYS_Platform':'win32'},'sysPaths':[]}
   pf=out/'builder.params';pf.write_text(json.dumps(params,indent=2),encoding='utf-8')
   run=subprocess.run([str(builder),'-p',str(pf),'--rebuild'],cwd=root,text=True,capture_output=True,timeout=180)
   text=run.stdout+run.stderr
   for old,new in [(str(temp),'<TEMP>'),(temp.as_posix(),'<TEMP>'),(str(ROOT),'<PROJECT>'),(ROOT.as_posix(),'<PROJECT>'),(str(sdk),'<SDK>'),(sdk.as_posix(),'<SDK>'),(str(tool),'<ARM_GCC>'),(str(ext),'<EIDE>')]:text=text.replace(old,new)
   reports+=['BOARD '+board,text]
   if run.returncode:
    print('\n'.join(reports[:4]));print('\n'.join(text.splitlines()[-100:]));return run.returncode
   elfs=list(out.glob('*.elf'))+list(out.glob('*.axf'))
   assert len(elfs)==1, f'Expected one linked ELF, found {elfs}'
   reports.append(subprocess.check_output([str(tool/'bin/arm-none-eabi-size.exe'),str(elfs[0])],text=True).replace(str(temp),'<TEMP>'))
   undefined=subprocess.check_output([str(tool/'bin/arm-none-eabi-nm.exe'),'-u',str(elfs[0])],text=True)
   assert not undefined.strip(),undefined
   symbols=subprocess.check_output([str(tool/'bin/arm-none-eabi-nm.exe'),'--defined-only',str(elfs[0])],text=True)
   required=({'st_cal_system_init','st_cal_endpoint_step','st_cal_fatfs_open','st_cal_fatfs_storage','st_f4_capture_bind_calibration','st_cal_input_offer_isr','st_f4_capture_complete','st_f4_capture_error','st_link_server_step','st_link_runtime_init'} if board=='Acquisition' else {'st_cal_remote_init','st_cal_remote_port','st_cal_remote_step','st2_client_cal_command','st_link_client_step','st_link_runtime_init'})
   defined={line.split()[-1] for line in symbols.splitlines() if line.split()}
   assert required<=defined,'Calibration integration discarded or missing: '+str(required-defined)
   reports.append('PASS retained calibration integration symbols: '+', '.join(sorted(required)))
   reports.append('PASS linked ELF with no undefined symbols; binaries removed after validation.')
 reports.append('Both EIDE targets linked. The entry point performs only a CRC self-check and WFI. No board functionality is claimed.')
 result='\n'.join(reports)+'\n';(ROOT/'Models/eide_link_validation.log').write_text(result,encoding='utf-8');print(result);return 0
if __name__=='__main__':raise SystemExit(main())
