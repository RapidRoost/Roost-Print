"""
Roost Print: Polymaker filaments for the shop's printers (Creality K2 Plus and K1C, 0.4 mm).

Rapid Roost is moving to Polymaker filament, so every Polymaker material gets a profile for each
printer, and those are the ones shown by default (the Creality ones stay installed, just hidden;
tick them back on in the filament list any time).

Each profile is built from two sources:
  * the printer side comes from Creality's own profile of the same kind of material for that
    printer (fans, cooling, chamber, start G-code all tuned for the machine), and
  * the material side (temperatures, flow, max flow rate, density, softening point, type) comes
    from Polymaker's own profiles in OrcaSlicer's filament library (tools/roost/polymaker/,
    copied from github.com/SoftFever/OrcaSlicer, resources/profiles/OrcaFilamentLibrary).
Where Polymaker gives no value the Creality one stays. Spool cost is left as Creality's default.

Creality's existing Polymaker profiles (made with Polymaker for the K2 Plus) are kept as they are.

Run from the repo root:  python tools/roost/polymaker.py   (safe to run again)
Writes resources/profiles/Creality/filament/*.json, the filament list in Creality.json, the
printers' default materials, and resources/profiles/Creality/roost_polymaker.json (the list the
app turns on at first start).
"""
import glob
import json
import os
from collections import OrderedDict

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, 'tools', 'roost', 'polymaker')
CREALITY = os.path.join(ROOT, 'resources', 'profiles', 'Creality')
FIL = os.path.join(CREALITY, 'filament')
BUNDLE = os.path.join(ROOT, 'resources', 'profiles', 'Creality.json')

PRINTERS = {
    'Creality K2 Plus 0.4 nozzle': 'Creality K2 Plus',
    'Creality K1C 0.4 nozzle': 'Creality K1C',
}
# Hottest nozzle each printer can run (materials that need more are left off that printer).
MAX_NOZZLE = {'Creality K2 Plus 0.4 nozzle': 350, 'Creality K1C 0.4 nozzle': 300}

# Material side: only these keys are taken from Polymaker's profiles.
MATERIAL_KEYS = [
    'filament_type', 'filament_flow_ratio', 'filament_density', 'temperature_vitrification',
    'filament_max_volumetric_speed', 'nozzle_temperature', 'nozzle_temperature_initial_layer',
    'nozzle_temperature_range_low', 'nozzle_temperature_range_high',
    'hot_plate_temp', 'hot_plate_temp_initial_layer', 'textured_plate_temp', 'textured_plate_temp_initial_layer',
    'eng_plate_temp', 'eng_plate_temp_initial_layer', 'cool_plate_temp', 'cool_plate_temp_initial_layer',
    'required_nozzle_HRC',
]

# Which Creality profile each material is built on (first one that exists for the printer).
def templates_for(name, ftype):
    n, t = name.lower(), (ftype or '').upper()
    if 'pps' in n:
        return ['Generic PPS-CF', 'Generic PPS']
    if t.startswith('PA') or 'fiberon pa' in n:
        if 'pa12' in n:
            return ['Generic PA12-CF', 'Generic PA-CF']
        if 'pa612' in n:
            return ['Generic PA612-CF', 'Generic PA-CF']
        if 'gf' in n:
            return ['Generic PA6-GF', 'Generic PA-CF']
        return ['Generic PA6-CF', 'Generic PA-CF']
    if 'pet-cf' in n:
        return ['Generic PET-CF', 'Generic PETG-CF']
    if 'petg-rcf' in n or 'petg-esd' in n:
        return ['Generic PETG-CF', 'Generic PETG']
    if 'petg' in n or 'cope' in n or t.startswith('PET'):
        return ['Generic PETG']
    if 'asa' in n:
        return ['Generic ASA']
    if 'abs' in n:
        return ['Generic ABS']
    if 'polysonic' in n:
        return ['Hyper PLA']
    if 'silk' in n:
        return ['Generic PLA-Silk', 'Generic PLA']
    if 'matte' in n:
        return ['CR-PLA Matte', 'Generic PLA']
    if '-gf' in n or '-cf' in n:
        return ['Generic PLA-CF', 'Generic PLA']
    return ['Generic PLA']


def type_from_name(name):
    """Upstream marks a few Fiberon materials as PLA; the name says what they really are."""
    n = name.lower()
    for key, t in (('pps-cf', 'PPS-CF'), ('-gf', None), ('pa', None), ('pet-cf', 'PET-CF'), ('petg-esd', 'PETG')):
        if key in n and t:
            return t
    if 'fiberon pa' in n:
        return 'PA-GF' if 'gf' in n else 'PA-CF' if 'cf' in n else 'PA'
    return None


def default_type(inherits):
    return {'fdm_filament_pa': 'PA', 'fdm_filament_pet': 'PETG', 'fdm_filament_abs': 'ABS',
            'fdm_filament_asa': 'ASA'}.get(inherits, 'PLA')


def load_polymaker():
    """Polymaker materials: name -> merged material settings (base + System overrides)."""
    base = {}
    for f in glob.glob(os.path.join(SRC, '*@base.json')):
        d = json.load(open(f, encoding='utf-8'))
        base[d['name']] = d
    def resolve(d):
        parent = base.get(d.get('inherits', ''))
        out = dict(resolve(parent)) if parent else {}
        out.update(d)
        return out
    mats = OrderedDict()
    for name in sorted(base):
        d = resolve(base[name])
        mat = name[:-len(' @base')]
        sysf = os.path.join(SRC, f'{mat} @System.json')
        if os.path.exists(sysf):
            d.update({k: v for k, v in json.load(open(sysf, encoding='utf-8')).items()
                      if k not in ('name', 'inherits', 'setting_id', 'instantiation')})
        top = base[name]['inherits']
        while top in base:
            top = base[top]['inherits']
        d['_top'] = top
        mats[mat] = d
    # Creality's own Polymaker profiles (K2 Plus) for materials the library doesn't have.
    for f in sorted(glob.glob(os.path.join(FIL, '* @Creality K2 Plus 0.4 nozzle.json'))):
        d = json.load(open(f, encoding='utf-8'))
        if d.get('filament_vendor') != ['Polymaker'] or 'Roost Print' in str(d.get('filament_notes', '')):
            continue
        mat = d['name'].split(' @')[0]
        if mat not in mats:
            d['_top'] = d.get('inherits', 'fdm_filament_pla')
            mats[mat] = d
    return mats


def main():
    bundle = json.load(open(BUNDLE, encoding='utf-8'), object_pairs_hook=OrderedDict)
    flist = bundle['filament_list']
    listed = {e['name'] for e in flist}
    made, kept = OrderedDict((p, []) for p in PRINTERS), 0
    for mat, d in load_polymaker().items():
        ftype = type_from_name(mat) or (d.get('filament_type') or [default_type(d['_top'])])[0]
        for printer in PRINTERS:
            short = printer.replace('Creality ', '')
            name = f'{mat} @Creality {short}'
            path = os.path.join(FIL, name + '.json')
            existing = os.path.exists(path) and 'Roost Print' not in json.load(open(path, encoding='utf-8')).get('filament_notes', '')
            if existing:  # Creality + Polymaker's own profile for this printer: keep it
                made[printer].append(mat)
                kept += 1
                continue
            temp = int(float((d.get('nozzle_temperature') or ['0'])[0] if isinstance(d.get('nozzle_temperature'), list) else d.get('nozzle_temperature', 0)))
            if temp > MAX_NOZZLE[printer]:
                if os.path.exists(path):
                    os.remove(path)
                flist[:] = [e for e in flist if e['name'] != name]
                listed.discard(name)
                continue
            tpl = None
            for t in templates_for(mat, ftype):
                tp = os.path.join(FIL, f'{t} @Creality {short}.json')
                if os.path.exists(tp):
                    tpl = json.load(open(tp, encoding='utf-8'), object_pairs_hook=OrderedDict)
                    break
            if tpl is None:
                print('no template for', mat, printer)
                continue
            out = OrderedDict()
            for k, v in tpl.items():
                out[k] = v
            out['name'] = name
            out['filament_id'] = d.get('filament_id', tpl.get('filament_id'))
            out.pop('setting_id', None)
            out['filament_vendor'] = ['Polymaker']
            for k in MATERIAL_KEYS:
                if k in d:
                    out[k] = d[k]
            out['filament_type'] = [ftype]
            out['compatible_printers'] = [printer]
            out.pop('compatible_printers_condition', None)
            out['filament_notes'] = f'Roost Print: Polymaker {mat}, tuned for the {short}.'
            json.dump(out, open(path, 'w', encoding='utf-8', newline='\n'), indent=4, ensure_ascii=False)
            made[printer].append(mat)
            if name not in listed:
                flist.append(OrderedDict([('name', name), ('sub_path', f'filament/{name}.json')]))
                listed.add(name)
    json.dump(bundle, open(BUNDLE, 'w', encoding='utf-8', newline='\n'), indent=4, ensure_ascii=False)

    # Printers offer Polymaker first (base names, as the model files list them).
    for printer, model in PRINTERS.items():
        mp = os.path.join(CREALITY, 'machine', model + '.json')
        m = json.load(open(mp, encoding='utf-8'), object_pairs_hook=OrderedDict)
        m['default_materials'] = ';'.join(made[printer])
        json.dump(m, open(mp, 'w', encoding='utf-8', newline='\n'), indent=4, ensure_ascii=False)

    # New jobs start on Polymaker PolyLite PLA.
    for printer in PRINTERS:
        pp = os.path.join(CREALITY, 'machine', printer + '.json')
        m = json.load(open(pp, encoding='utf-8'), object_pairs_hook=OrderedDict)
        m['default_filament_profile'] = [f'PolyLite PLA @{printer}']
        json.dump(m, open(pp, 'w', encoding='utf-8', newline='\n'), indent=4, ensure_ascii=False)

    # The full preset names the app turns on at first start.
    on = [f'{mat} @{p}' for p, mats in made.items() for mat in mats]
    json.dump({'filaments': on}, open(os.path.join(CREALITY, 'roost_polymaker.json'), 'w', encoding='utf-8', newline='\n'), indent=4)
    print(f'{sum(len(v) for v in made.values())} Polymaker profiles ({kept} kept from Creality)')


if __name__ == '__main__':
    main()
