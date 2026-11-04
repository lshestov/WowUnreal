"""Rendered character creation on a fresh, fingerprint-owned disposable account."""
from __future__ import annotations
import argparse
import base64
import hashlib
import importlib.util
import re
import secrets
import subprocess
import sys
from pathlib import Path


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--root',type=Path,default=Path(r'D:\Games\idle_wow'))
    parser.add_argument('--name',required=True)
    parser.add_argument('--race',type=int,default=11)
    parser.add_argument('--class-id',type=int,default=7)
    parser.add_argument('--gender',type=int,choices=(0,1),default=1)
    parser.add_argument('--cleanup-only',action='store_true')
    parser.add_argument('--baseline-approval',type=Path,help=argparse.SUPPRESS)  # Accepted by older launchers.
    args=parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_-]+',args.name): raise RuntimeError('Invalid evidence name')
    if (args.race,args.class_id) not in ((1,1),(11,7),(7,8),(6,11),(10,2),(5,4)):
        raise RuntimeError('Native suite supports only declared Wrath combinations')
    root=args.root.resolve()
    spec=importlib.util.spec_from_file_location('creation_primary',root/'scripts/test-wowunreal-ui-primary.py')
    primary=importlib.util.module_from_spec(spec);spec.loader.exec_module(primary);peer=primary.peer
    fixture_path=root/'runtime/private/wowunreal-creation-native-fixture.json'
    previous=peer.read_json(fixture_path)
    saved_primary=peer.read_json(primary.FIXTURE)
    if saved_primary and not saved_primary.get('closed'): raise RuntimeError('An owned primary fixture requires recovery first')
    output=root/'logs/wowunreal-character-creation'/args.name
    exe=root/'runtime/client-wowunreal/WowUnreal/Binaries/Win64/WowUnreal.exe'
    check=subprocess.run([str(peer.PWSH),'-NoLogo','-NoProfile','-Command',
        "$p=@(Get-CimInstance Win32_Process|Where-Object {$_.Name -match '^WowUnreal.exe$|^UnrealEditor.*exe$|^UnrealBuildTool.exe$|^ShaderCompileWorker.exe$'}); if($p.Count){exit 2}"],capture_output=True)
    if check.returncode: raise RuntimeError('Another UE process is running; preserved')
    process=None
    def cleanup(fixture):
        nonlocal process
        if process and process.poll() is None:
            process.kill();process.wait(timeout=30)
        if peer.owned_account(fixture) not in (None,fixture['accountId']): raise RuntimeError('Owned registration identity changed')
        peer.wait_offline(int(fixture.get('guid',0)),fixture['accountId'])
        rows=peer.sql(f"SELECT guid,name,online,race,class,gender FROM idlewow_characters.characters WHERE account={fixture['accountId']};")
        if len(rows)>1 or any(row[1]!=fixture['character'] or row[2]!='0' or row[3:]!=[str(fixture['race']),str(fixture['class']),str(fixture['gender'])] for row in rows): raise RuntimeError('Refusing cleanup of an unexpected character')
        if rows:
            guid=int(rows[0][0])
            if guid in fixture.get('protectedGuids',()) or fixture.get('guid',guid)!=guid: raise RuntimeError('Refusing protected/mismatched GUID')
            fixture['guid']=guid;peer.write_json(fixture_path,fixture)
        # Original helper retains its one-character, registration proof,
        # offline and protocol-deletion guards. No expanded deletion whitelist.
        primary.remove_registered_account(fixture,saved_primary or {},args.name)
        credential=Path(fixture['userDir'])/'Saved/WowCredentials.json'
        credential.unlink(missing_ok=True)
        fixture.update(password='',closed=True,restored=True)
        peer.write_json(fixture_path,fixture)
    if args.cleanup_only:
        if previous and not previous.get('closed'): cleanup(previous)
        return 0
    if previous and not previous.get('closed'): raise RuntimeError('An owned creation fixture requires --cleanup-only')
    if output.exists(): raise RuntimeError('Use a new evidence name')
    if peer.sql('SELECT COUNT(*) FROM idlewow_characters.characters WHERE online<>0;')[0][0]!='0': raise RuntimeError('An empty realm is required')
    output.mkdir(parents=True)
    account='QA_UI_'+secrets.token_hex(4).upper();password=secrets.token_hex(16).upper();salt=secrets.token_bytes(32)
    exponent=int.from_bytes(hashlib.sha1(salt+hashlib.sha1((account+':'+password).encode()).digest()).digest(),'little')
    modulus=int('894B645E89E1535BBDAD5B8B290650530801B18EBFBF5E8FAB3C82872A3E9BB7',16)
    verifier=pow(7,exponent,modulus).to_bytes(32,'little');token=secrets.token_hex(16)
    fixture={'account':account,'password':password,'character':peer.make_character_name(),'class':args.class_id,'race':args.race,'gender':args.gender,
        'token':token,'registrationHash':hashlib.sha256(salt+verifier).hexdigest(),'closed':False,
        'userDir':str(root/'runtime/private'/('wowunreal-creation-'+token))}
    peer.write_json(fixture_path,fixture)
    if peer.sql(f"SELECT id FROM idlewow_auth.account WHERE username='{account}';"): raise RuntimeError('Fresh registration collision')
    registered=peer.sql(f"INSERT INTO idlewow_auth.account (username,salt,verifier,expansion) VALUES ('{account}',UNHEX('{salt.hex()}'),UNHEX('{verifier.hex()}'),2); SELECT LAST_INSERT_ID();")
    fixture['accountId']=int(registered[0][0]);peer.write_json(fixture_path,fixture)
    success=False
    try:
        directory=Path(fixture['userDir'])/'Saved';directory.mkdir(parents=True,exist_ok=False)
        encoded=base64.b64encode(bytes(v^0xa5^(i&255) for i,v in enumerate(password.encode()))).decode()
        peer.write_json(directory/'WowCredentials.json',[{'alias':account,'username':account,'server':'127.0.0.1','port':3724,'default':True,'pw':encoded}])
        scenario={'output':str(output),'character':fixture['character'],'account':account,'race':args.race,'class':args.class_id,'gender':args.gender}
        specpath=output/'steps.json';peer.write_json(specpath,scenario)
        exe_hash=hashlib.sha256(exe.read_bytes()).hexdigest()
        arguments=[str(exe),f'-wowdata={root}/runtime/client-enUS-3.3.5a/Data',f'-UserDir={fixture["userDir"]}',
            '-autologin',f'-account={account}',f'-charactercreationprobe={specpath}',
            '-windowed','-ResX=1280','-ResY=900','-NoSound','-nosplash','-unattended',f'-abslog={output}/client.log']
        with (output/'console.log').open('w',encoding='utf-8') as log:
            process=subprocess.Popen(arguments,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
            fixture['pid']=process.pid;peer.write_json(fixture_path,fixture)
            code=process.wait(timeout=210)
        report=peer.read_json(output/'probe.json')
        if code or not report or not report.get('passed') or not report.get('neverEnteredWorld'): raise RuntimeError('Rendered native creation failed; inspect the local report')
        if hashlib.sha256(exe.read_bytes()).hexdigest()!=exe_hash: raise RuntimeError('Executable changed during native verification')
        fixture['guid']=int(report['guid']);peer.write_json(fixture_path,fixture)
        peer.wait_offline(fixture['guid'],fixture['accountId'])
        saved=peer.sql(f"SELECT account,name,race,class,gender,skin,face,hairStyle,hairColor,facialStyle,online FROM idlewow_characters.characters WHERE guid={fixture['guid']};")
        expected=[str(fixture['accountId']),fixture['character']]+[str(report[key]) for key in ('race','class','gender','skin','face','hairStyle','hairColor','facialHair')]+['0']
        if saved!=[expected]: raise RuntimeError('Database appearance/ownership differs from the enum and preview')
        peer.write_json(output/'native-verification.json',{'passed':True,'sha256':exe_hash,'databaseAppearanceMatches':True,'enumMatchesPreview':True,'stockSlateControlsInvoked':bool(report.get('stockSlateControlsInvoked')),
            'shortNameResult':report['shortNameResult'],'deathKnightResult':report['deathKnightResult'],'duplicateNameResult':report['duplicateNameResult'],'requestsSent':report['requestsSent']})
        success=True
    finally:
        cleanup(fixture)
    peer.write_json(output/'cleanup.json',{'closed':fixture['closed'],'restored':fixture['restored'],
        'credentialsRemoved':not (Path(fixture['userDir'])/'Saved/WowCredentials.json').exists(),'sha256':exe_hash})
    print(f'PASS {args.name}: rendered UI, exact server/database appearance, owned account removed',flush=True)
    return 0 if success else 1

if __name__=='__main__':
    try:sys.exit(main())
    except Exception as error:
        print(f'Owned creation verification stopped: {error}',file=sys.stderr);sys.exit(1)
