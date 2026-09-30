"""Generate UE localization inputs from native FText keys and the Chinese catalog.

Run this script, then use the UE GatherText commandlet with the printed config.
The .locres files are built by UE, not by a custom runtime language switch.
"""
import json, re
from pathlib import Path

root = Path(__file__).resolve().parent.parent
target = root/'Content/Localization/FModelAnimRestore'
target.mkdir(parents=True,exist_ok=True)
zh = json.loads((root/'Localization/zh-Hans.json').read_text(encoding='utf-8'))
found = {}
pattern = re.compile(r'(?:NSLOCTEXT\("FModelAnimRestore",\s*|(?<!NS)LOCTEXT\()\s*("(?:[^"\\]|\\.)*")\s*,\s*("(?:[^"\\]|\\.)*")\s*\)')
for path in (root/'Source').rglob('*.cpp'):
    for match in pattern.finditer(path.read_text(encoding='utf-8')):
        key, text = map(json.loads,match.groups())
        if key in found:
            assert found[key]['text']==text, key
        found[key]={'text':text,'path':str(path.relative_to(root)).replace('\\','/')}
assert set(found) <= set(zh), 'Missing translations: '+str(set(found)-set(zh))
manifest = {'FormatVersion':1,'Namespace':'','Subnamespaces':[{'Namespace':'FModelAnimRestore','Children':[
    {'Source':{'Text':entry['text']},'Keys':[{'Key':key,'Path':entry['path']}]} for key,entry in sorted(found.items())]}]}
(target/'FModelAnimRestore.manifest').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
for culture in ('en','zh-Hans'):
    folder=target/culture
    folder.mkdir(exist_ok=True)
    archive={'FormatVersion':2,'Namespace':'','Subnamespaces':[{'Namespace':'FModelAnimRestore','Children':[
        {'Key':key,'Source':{'Text':entry['text']},'Translation':{'Text':entry['text'] if culture=='en' else zh[key]}}
        for key,entry in sorted(found.items())]}]}
    (folder/'FModelAnimRestore.archive').write_text(json.dumps(archive,ensure_ascii=False,indent=2),encoding='utf-8')
config=root/'Localization/Compile.ini'
config.write_text('[CommonSettings]\nSourcePath='+target.as_posix()+'\nDestinationPath='+target.as_posix()+'''
ManifestName=FModelAnimRestore.manifest
ArchiveName=FModelAnimRestore.archive
ResourceName=FModelAnimRestore.locres
NativeCulture=en
CulturesToGenerate=en
CulturesToGenerate=zh-Hans
bValidateFormatPatterns=true
bValidateSafeWhitespace=true

[GatherTextStep0]
CommandletClass=GenerateTextLocalizationResource
''',encoding='utf-8')
print(f'{len(found)} native FText keys; config: {config}')
