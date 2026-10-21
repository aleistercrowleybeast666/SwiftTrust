"""Run the installed EIDE native builder on both actual project source lists.
Objects/cache are temporary and removed; --artifacts retains sparse HEX/ELF only.
Does not flash, enumerate probes/COM ports or open hardware.
Requires SDK_ROOT and ARM_GCC_ROOT; EIDE_EXTENSION may override extension discovery.
"""
import datetime,json,os,subprocess,tempfile,copy,sys,hashlib,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
 sdk=Path(os.environ['SDK_ROOT']).resolve();tool=Path(os.environ['ARM_GCC_ROOT']).resolve()
 extensions=sorted((Path.home()/'.vscode/extensions').glob('cl.eide-*'))
 ext=Path(os.environ.get('EIDE_EXTENSION',str(extensions[-1]) if extensions else '')).resolve()
 builder=ext/'res/tools/win32/unify_builder/unify_builder.exe'
 manifest=json.loads((ROOT/'Models/official_header_manifest.json').read_text(encoding='utf-8'))
 for entry in manifest:
  path=(sdk/entry['path']).resolve()
  assert path.is_relative_to(sdk) and path.is_file(),'Missing SDK input: '+entry['path']
  assert hashlib.sha256(path.read_bytes()).hexdigest()==entry['sha256'],'SDK hash mismatch: '+entry['path']
 reports=[datetime.datetime.now(datetime.timezone.utc).isoformat(),
  'Actual EIDE ReferenceApp application targets; complete main/HAL/IRQ integration, offline build only.',
  'No hardware access. Objects in auto-removed temp directories; optional sparse HEX/ELF retained.',
  'PASS '+str(len(manifest))+' official SDK inputs match checked-in manifest SHA256.']
 with tempfile.TemporaryDirectory(prefix='swift-eide-') as d:
  temp=Path(d)
  for board,cpu in [('Acquisition','cortex-m4'),('Controller','cortex-m0+')]:
   root=ROOT/board;project=json.loads((root/'.eide/eide.yml').read_text());target=project['targets']['ReferenceApp']
   def expand(s):return s.replace('${SDK_ROOT}',sdk.as_posix())
   options=copy.deepcopy(target['toolchainConfigMap']['GCC']['options'])
   options['global'].update({'toolPrefix':'arm-none-eabi-','microcontroller-cpu':cpu,'microcontroller-fpu':cpu,'microcontroller-float':cpu,'$arch-extensions':'','$clang-arch-extensions':'','$armlink-arch-extensions':''})
   options['linker']['link-scatter']=[(root/'board/reference_memory.ld').as_posix()]
   options['linker']['$toolName']='gcc'
   out=temp/board;out.mkdir();sources=[expand(f['path']) for f in project['virtualFolder']['files']]
   assert all((root/s).is_file() for s in sources)
   params={'name':project['name'],'target':'ReferenceApp','toolchain':'GCC','toolchainLocation':str(tool),
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
   required=({'main','SystemClock_Config','HAL_SPI_MspInit','HAL_SD_MspInit','HAL_TIM_Base_MspInit','EXTI4_IRQHandler','DMA2_Stream0_IRQHandler','DMA2_Stream5_IRQHandler','DMA2_Stream3_IRQHandler','DMA2_Stream6_IRQHandler','SDIO_IRQHandler','SPI1_IRQHandler','TIM5_IRQHandler','ADC_IRQHandler','USART1_IRQHandler','SysTick_Handler','HAL_SPI_TxRxCpltCallback','st_f4_capture_drdy','st_fatfs_worker_step','st_cal_endpoint_step','st_link_server_step','st_journal_open','st_console_feed'} if board=='Acquisition' else {'main','SystemClock_Config','HAL_SPI_MspInit','HAL_I2C_MspInit','HAL_TIM_Encoder_MspInit','EXTI4_15_IRQHandler','I2C1_IRQHandler','ADC1_COMP_IRQHandler','USART1_IRQHandler','SysTick_Handler','st_ui2_handle','st_ui2_render','st_oled_service','st_cal_remote_step','st_link_client_step','st_journal_open','st_console_feed'})
   defined={line.split()[-1] for line in symbols.splitlines() if line.split()}
   assert required<=defined,'Application entry/IRQ missing: '+str(required-defined)
   assert all(line.split()[-2]=='T' for line in symbols.splitlines() if line.split() and line.split()[-1] in required),'Weak default handler retained'
   reports.append('PASS retained application entry/IRQ symbols: '+', '.join(sorted(required)))
   reports.append('PASS linked application ELF, actual board handlers, no undefined symbols.')
   # Verify every loadable image interval avoids the reserved physical NV bytes.
   sections=subprocess.check_output([str(tool/'bin/arm-none-eabi-objdump.exe'),'-h',str(elfs[0])],text=True).splitlines()
   nv=(0x08008000,0x08010000) if board=='Acquisition' else (0x0801F000,0x08020000)
   for i,line in enumerate(sections[:-1]):
    fields=line.split()
    if len(fields)>=7 and fields[0].isdigit() and 'LOAD' in sections[i+1]:
     size=int(fields[2],16);load=int(fields[4],16)
     assert not(size and load<nv[1] and load+size>nv[0]),'Image overlaps NV: '+line
   reports.append('PASS load sections exclude NV region '+hex(nv[0])+'..'+hex(nv[1]))
   if '--artifacts' in sys.argv:
    dest=ROOT/'dist'/board;dest.mkdir(parents=True,exist_ok=True)
    name=board+'_ReferenceApp'
    shutil.copy2(elfs[0],dest/(name+'.elf'))
    subprocess.check_call([str(tool/'bin/arm-none-eabi-objcopy.exe'),'-O','ihex',str(elfs[0]),str(dest/(name+'.hex'))])
    reports.append('Sparse HEX + ELF saved in dist/'+board+' (no BIN spanning NV holes).')
   headers=sorted({p for folder in ['Common/Inc','Acquisition/include','Acquisition/ports','Controller/include','Controller/ports',board+'/board'] for p in (ROOT/folder).rglob('*.h')})
   for source in sources+[str(root/'board/reference_memory.ld'),str(root/'.eide/eide.yml')]+list(map(str,headers)):
    path=(root/source).resolve()
    if path.is_relative_to(ROOT): reports.append('SHA256 '+str(path.relative_to(ROOT)).replace('\\','/')+' '+hashlib.sha256(path.read_bytes()).hexdigest())
 if '--artifacts' in sys.argv:
  artifacts=[]
  for path in sorted((ROOT/'dist').rglob('*')):
   if path.is_file() and path.suffix in ('.elf','.hex'):
    artifacts.append({'path':path.relative_to(ROOT).as_posix(),'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
  (ROOT/'dist/manifest.json').write_text(json.dumps({'built_utc':reports[0],'target':'ReferenceApp','hardware_tested':False,'artifacts':artifacts},indent=2)+'\n',encoding='utf-8')
 reports.append('Both reference applications linked. No hardware run, flashing, throughput or electrical validation is claimed.')
 result='\n'.join(reports)+'\n';(ROOT/'Models/reference_arm_validation.log').write_text(result,encoding='utf-8');print('PASS both ReferenceApp ARM builds; see Models/reference_arm_validation.log');return 0
if __name__=='__main__':raise SystemExit(main())
