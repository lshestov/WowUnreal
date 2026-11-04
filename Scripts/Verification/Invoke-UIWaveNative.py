"""Coordinate native routes and real client restarts on a disposable QA account.

Uses the host's existing primary lifecycle/cleanup helper. No credentials or
database rows are copied into the evidence directory.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(r'D:\Games\idle_wow'))
    parser.add_argument('--spec', required=True, type=Path)
    parser.add_argument('--restart-spec', action='append', default=[], type=Path)
    parser.add_argument('--addon-dir', action='append', default=[], type=Path)
    parser.add_argument('--baseline-approval', help=argparse.SUPPRESS)  # Older launch commands pass this option.
    parser.add_argument('--name', required=True)
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_-]+', args.name) or len(args.restart_spec) > 3:
        raise RuntimeError('A bounded route and a unique evidence name are required')
    root = args.root.resolve()
    output = root / 'logs/wowunreal-ui-wave' / args.name
    if output.exists():
        raise RuntimeError('Use a new evidence name')
    module_spec = importlib.util.spec_from_file_location(
        'owned_primary', root / 'scripts/test-wowunreal-ui-primary.py')
    primary = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(primary)
    peer = primary.peer
    previous = peer.read_json(primary.FIXTURE)
    if primary.FIXTURE.exists() and (not previous or not previous.get('closed') or not previous.get('restored')):
        raise RuntimeError('The previous disposable QA account still needs cleanup')
    specs = [args.spec.resolve(), *(p.resolve() for p in args.restart_spec)]
    spec_hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in specs}
    for spec in specs:
        value = peer.read_json(spec)
        if not value or not isinstance(value.get('steps'), list):
            raise RuntimeError('Every route requires a steps list')
    exe = root / 'runtime/client-wowunreal/WowUnreal/Binaries/Win64/WowUnreal.exe'
    exe_hash = hashlib.sha256(exe.read_bytes()).hexdigest()
    output.mkdir(parents=True)
    runs = []
    original_cleanup = primary.cleanup
    installed = []

    def install_addons() -> None:
        config = peer.read_json(root / 'runtime/client-wowunreal/idle-wow.json')
        addons_root = (Path(config['DataPath']) / 'Interface/AddOns').resolve()
        for source in args.addon_dir:
            source = source.resolve()
            if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*', source.name) \
                    or not (source / (source.name + '.toc')).is_file():
                raise RuntimeError('A named custom QA addon with its matching TOC is required')
            destination = addons_root / source.name
            if destination.exists() or destination.is_symlink():
                raise RuntimeError('QA addon destination already exists; existing files preserved')
            files = sorted(p for p in source.rglob('*') if p.is_file())
            if any(p.is_symlink() or p.suffix.lower() not in ('.lua', '.toc', '.xml') for p in files):
                raise RuntimeError('QA addon must contain only authored Lua, TOC and XML files')
            destination.mkdir(parents=True)
            owned = {'directory': destination, 'files': []}
            installed.append(owned)
            for file in files:
                target = destination / file.relative_to(source)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(file, target)
                owned['files'].append((target, hashlib.sha256(target.read_bytes()).hexdigest()))

    def remove_addons() -> None:
        for addon in reversed(installed):
            directory = addon['directory']
            expected = {path for path, _ in addon['files']}
            actual = {p for p in directory.rglob('*') if p.is_file()}
            if actual != expected or any(hashlib.sha256(p.read_bytes()).hexdigest() != h
                                         for p, h in addon['files']):
                raise RuntimeError('QA addon contents changed; files preserved for review')
            for path, _ in addon['files']:
                path.unlink()
            for child in sorted((p for p in directory.rglob('*') if p.is_dir()),
                                key=lambda p: len(p.parts), reverse=True):
                child.rmdir()
            directory.rmdir()

    def capture_evidence(fixture: dict) -> None:
        run = Path(fixture['lastRun'])
        report = peer.read_json(run / 'probe.json')
        if not report or not report.get('passed'):
            raise RuntimeError('A native route failed; inspect its probe report')
        captures = output / f'captures-{len(runs)}.json'
        subprocess.run([str(peer.PWSH), '-NoLogo', '-NoProfile', '-File',
                        str(root / 'logs/wowunreal-ui-completion/Test-Captures.ps1'),
                        '-RunPath', str(run), '-ReportPath', str(captures)], check=True,
                       creationflags=subprocess.CREATE_NO_WINDOW)
        runs.append({'run': str(run), 'steps': report['completedSteps'], 'captures': str(captures)})

    def finish_with_restarts(fixture: dict, name: str) -> None:
        try:
            if not fixture.get('lastRun'):
                return
            capture_evidence(fixture)
            for index, path in enumerate(specs[1:], 1):
                peer.wait_offline(fixture['guid'], fixture.get('accountId'))
                scenario = peer.read_json(path)
                scenario['character'] = fixture['character']
                generated = output / f'restart-{index}.json'
                manifest = output / f'restart-{index}-process.json'
                peer.write_json(generated, scenario)
                fixture['manifest'] = str(manifest)
                peer.write_json(primary.FIXTURE, fixture)
                with (output / f'restart-{index}-console.log').open('w', encoding='utf-8') as log:
                    result = subprocess.run([str(peer.PWSH), '-NoLogo', '-NoProfile', '-File',
                        str(root / 'scripts/test-wowunreal-gameplay.ps1'), '-RequireEmptyRealm',
                        '-StepsFile', str(generated), '-ProcessManifest', str(manifest),
                        '-UserDir', fixture['userDir'], '-Width', str(scenario.get('width', 1280)),
                        '-Height', str(scenario.get('height', 900))], stdout=log,
                        stderr=subprocess.STDOUT, timeout=850, creationflags=subprocess.CREATE_NO_WINDOW)
                owned = peer.read_json(manifest)
                if result.returncode or not owned:
                    raise RuntimeError('Restart route failed; inspect its console and probe reports')
                fixture['lastRun'] = owned['output']
                peer.write_json(primary.FIXTURE, fixture)
                capture_evidence(fixture)
        finally:
            # The original helper waits for logout, verifies registration ownership,
            # deletes only this fixture and removes its credentials.
            original_cleanup(fixture, name)

    primary.cleanup = finish_with_restarts
    argv = sys.argv
    try:
        install_addons()
        sys.argv = [str(primary.__file__), '--steps', str(specs[0]), '--name', args.name]
        result = primary.main()
    finally:
        sys.argv = argv
        primary.cleanup = original_cleanup
        remove_addons()
    fixture = peer.read_json(primary.FIXTURE)
    if result or not fixture or not fixture.get('closed') or not fixture.get('restored') \
            or fixture.get('password') or (Path(fixture['userDir']) / 'Saved/WowCredentials.json').exists():
        raise RuntimeError('Native verification/owned cleanup failed')
    if hashlib.sha256(exe.read_bytes()).hexdigest() != exe_hash:
        raise RuntimeError('Executable changed during verification')
    if any(hashlib.sha256(p.read_bytes()).hexdigest() != spec_hashes[str(p)] for p in specs):
        raise RuntimeError('Replay specification changed during verification')
    peer.write_json(output / 'index.json', {
        'passed': True, 'sha256': exe_hash, 'runs': runs,
        'specs': [{'path': str(p), 'sha256': spec_hashes[str(p)]} for p in specs],
        'ownedFixturesClosed': True, 'credentialsRemoved': True,
        'fullClientRestarts': len(specs) - 1,
        'customQaAddonsRemoved': [str(item['directory']) for item in installed]})
    print(json.dumps({'passed': True, 'evidence': str(output / 'index.json')}))
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception as error:
        print(f'UI wave verification stopped: {error}', file=sys.stderr)
        sys.exit(1)
