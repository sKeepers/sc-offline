"""Validate UTF-8 catalogs, exact printf arguments, stable IDs, and source key coverage."""
import argparse,json,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
FMT=re.compile(r'%(?:[-+ #0]*\d*(?:\.\d+)?(?:hh|ll|I64|[hljztL])?[diuoxXfFeEgGaAcspn]|%)')
def formats(value):
 out=[];at=0
 while (at:=value.find('%',at))>=0:
  m=FMT.match(value,at)
  if not m:raise ValueError('invalid printf escape')
  out.append(m[0]);at=m.end()
 return out
def decode(s):
 def sub(m):
  if m[1] not in 'nrt\\s':raise ValueError('invalid catalog escape')
  return {'n':'\n','r':'\r','t':'\t','\\':'\\','s':' '}[m[1]]
 return re.sub(r'\\(.)',sub,s)
def read(path):
 out={}
 for line in path.read_text(encoding='utf-8-sig').splitlines():
  if not line.strip() or line.lstrip()[0] in '#;':continue
  if '=' not in line:raise ValueError(f'{path.name}: missing =')
  key,value=line.split('=',1);key=key.strip();value=decode(value.strip())
  if not re.fullmatch(r'[a-zA-Z0-9_.-]+',key) or key in out or not value:raise ValueError(f'{path.name}: invalid/duplicate/empty key {key}')
  out[key]=value
 return out
def check():
 folder=ROOT/'localization/launcher';english=read(folder/'en.ini')
 source='\n'.join(p.read_text(encoding='utf-8') for p in (ROOT/'launcher').glob('*') if p.suffix in ['.cpp','.h'])
 used=set(re.findall(r'"(launcher\.[a-z0-9_.]+)"',source))
 used.discard('launcher.log')  # A protocol/file name, not an interface key.
 assert not used-english.keys(),f'Missing English keys: {used-english.keys()}'
 assert not english.keys()-used,f'Obsolete English keys: {english.keys()-used}'
 compiled=dict((json.loads(k),json.loads(v)) for k,v in re.findall(r'catalog.Add\(("(?:\\.|[^"\\])*"),("(?:\\.|[^"\\])*")\);',(ROOT/'local/launcher_catalog.inc').read_text(encoding='utf-8')))
 assert compiled==english,'Compiled fallback differs from en.ini'
 for file in folder.glob('*.ini'):
  translated=read(file);assert not translated.keys()-english.keys(),f'{file.name}: obsolete keys'
  missing=english.keys()-translated.keys()
  if file.stem in ['en','ru']:assert not missing,f'{file.name}: missing keys'
  for key,text in translated.items():
   assert formats(text)==formats(english[key]),f'{file.name}: placeholders {key}'
   suffix=lambda s:s[s.index('##'):] if '##' in s and '\n' not in s else ''
   assert suffix(text)==suffix(english[key]),f'{file.name}: changed UI ID {key}'
  print(f'PASS {file.name}: {len(translated)} keys; {len(missing)} English fallbacks')
 print(f'PASS compiled English and source coverage: {len(used)} keys')
def fonts():
 from fontTools.ttLib import TTFont
 folder=Path('C:/Windows/Fonts');base=TTFont(folder/'segoeui.ttf').getBestCmap()
 required=set(ord(c) for f in (ROOT/'localization/launcher').glob('*.ini') for c in ''.join(read(f).values()) if c.isprintable())
 missing=required-base.keys();assert not missing,f'Segoe UI missing: {sorted(missing)}'
 for file,sample in [('msyh.ttc','中文'),('msgothic.ttc','日本語'),('malgun.ttf','한국어')]:
  cmap=TTFont(folder/file,fontNumber=0).getBestCmap();assert not set(map(ord,sample))-(base.keys()|cmap.keys())
 print(f'PASS font cmap: {len(required)} catalog characters; CJK fallback samples. Rendering not verified.')
if __name__=='__main__':
 parser=argparse.ArgumentParser();parser.add_argument('--fonts',action='store_true');args=parser.parse_args();check()
 if args.fonts:fonts()
