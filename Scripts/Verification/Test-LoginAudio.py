"""Inspect the actual UE master-submix recording, without an external decoder."""
import argparse
import array
import json
import math
from pathlib import Path
import statistics
import wave

p = argparse.ArgumentParser()
p.add_argument('--run', required=True, type=Path)
args = p.parse_args()
report = json.loads((args.run/'runtime.json').read_text(encoding='utf-8-sig'))
log = (args.run/'client.log').read_text(encoding='utf-8-sig')
assert 'GLUE_MP3_DECODED:' in log and 'duration=544.080' in log
assert "Initializing audio mixer using platform API: 'XAudio2'" in log
assert 'Using Audio Hardware Device ' in log and 'device=active' in log
assert '-nosound' not in log.lower().split('command line:')[-1].split('\n')[0]
with wave.open(str(args.run/'mixer.wav')) as wav:
    assert wav.getsampwidth() == 2 and wav.getnchannels() == 2
    rate = wav.getframerate()
    pcm = array.array('h', wav.readframes(wav.getnframes()))
    duration = wav.getnframes()/rate

def rms(values):
    return math.sqrt(sum(x*x for x in values)/max(1, len(values)))/32768

result = {'passed': False, 'hardwareMixer': 'XAudio2', 'rate': rate,
          'channels': 2, 'duration': duration, 'rms': rms(pcm),
          'peak': max(abs(x) for x in pcm)/32768}
assert duration > 3 and result['rms'] > .0001 and result['peak'] > .001
rows = report['steps']
if any(s['action'] == 'musicOff' for s in rows):
    # The master submix starts recording when the first source is ready.
    # Its lead-in is measured from the real procedural source's sample cursor.
    offset = statistics.median(s['elapsed']-s['pcmBytes']/192000 for s in rows
                              if s['action'] == 'capture' and s['elapsed'] < 69 and s['pcmBytes'] > 0)
    def row(action): return next(s for s in rows if s['action'] == action)
    def interval(action):
        at = row(action)['elapsed']-offset+.7
        samples = pcm[int(at*rate*2):int((at+1)*rate*2)]
        assert len(samples) > rate
        return rms(samples)
    levels = {key: interval(key) for key in ['musicOff', 'musicOn', 'quiet', 'normal', 'masterOff', 'masterOn']}
    assert levels['musicOff'] < .00005, levels
    assert levels['masterOff'] < .00005, levels
    assert min(levels[k] for k in ['musicOn', 'quiet', 'normal', 'masterOn']) > .0001, levels
    assert levels['quiet'] < levels['normal']*.8, levels
    assert row('musicOff')['playing'] and row('musicOn')['playing']
    assert row('musicOn')['pcmBytes'] > row('musicOff')['pcmBytes']+192000*2
    assert row('musicOff')['gain'] == row('masterOff')['gain'] == 0
    assert abs(row('quiet')['gain']-.1) < .00001
    loading = row('loading')
    assert not loading['playing'] and loading['pcmBytes'] == loading['effects'] == 0
    assert any(s['playing'] and s['pcmBytes'] > 0 for s in rows if s['elapsed'] > row('login')['elapsed']+1)
    result.update(clockOffset=offset, levels=levels, continuedAcrossMute=True,
                  stoppedForWorldEntry=True, restartedOnReturn=True)
result['passed'] = True
(args.run/'audio.json').write_text(json.dumps(result, indent=2))
print(json.dumps(result))
