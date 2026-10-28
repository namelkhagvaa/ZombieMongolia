# Tripo3D-ийн баавгайн загварыг (ястай, анимацигүй) тоглоомд бэлтгэнэ:
#   assets/brown_bear.glb  ->  assets/bear_mesh.glb   (яс, жин, UV; текстургүй тул хурдан ачаална)
#                              assets/bear_color.png  (өнгөний текстур 1024 px PNG)
# Ясыг үүргээр нь нэрлэнэ (тоглоом нэрээр нь хөдөлгөнө: BearModel.cpp). Баавгай +X зүг рүү харсан, урт ~1 нэгж.
# Tripo-гийн rig: урд зүүн хөлийн мөр нь Spine_4, урд баруун хөлийн мөр нь 0_Right_Limb_0 (гинж нэг үеэр урт).
#
# Хэрэглээ:  python tools/prep_bear.py
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fix_horse_rig import compact, export_texture, read_glb, write_glb  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, 'assets', 'brown_bear.glb')
TARGET = os.path.join(ROOT, 'assets', 'bear_mesh.glb')
TEXTURE = os.path.join(ROOT, 'assets', 'bear_color.png')

RENAME = {
    'tripo::Root': 'Root',
    'tripo::Spine_0': 'Pelvis', 'tripo::Spine_1': 'Back_0', 'tripo::Spine_2': 'Back_1', 'tripo::Spine_3': 'Chest',
    'tripo::Head_0': 'Neck', 'tripo::Head_1': 'Head', 'tripo::Head_2': 'Snout', 'bone_8': 'Ear',
    'tripo::Spine_4': 'FL_Shoulder', 'tripo::0_Left_Limb_0': 'FL_Elbow', 'tripo::0_Left_Limb_1': 'FL_Wrist',
    'tripo::0_Left_Limb_2': 'FL_Paw', 'tripo::0_Left_Limb_3': 'FL_Toe',
    'tripo::0_Right_Limb_0': 'FR_Shoulder', 'tripo::0_Right_Limb_1': 'FR_Elbow', 'tripo::0_Right_Limb_2': 'FR_Wrist',
    'tripo::0_Right_Limb_3': 'FR_Paw', 'tripo::0_Right_Limb_4': 'FR_Toe',
    'tripo::1_Left_Limb_0': 'HL_Hip', 'tripo::1_Left_Limb_1': 'HL_Knee', 'tripo::1_Left_Limb_2': 'HL_Ankle',
    'tripo::1_Left_Limb_3': 'HL_Paw',
    'tripo::1_Right_Limb_0': 'HR_Hip', 'tripo::1_Right_Limb_1': 'HR_Knee', 'tripo::1_Right_Limb_2': 'HR_Ankle',
    'tripo::1_Right_Limb_3': 'HR_Paw',
    'bone_19': 'Belly', 'bone_20': 'Tail_0', 'bone_21': 'Tail_1', 'bone_22': 'Tail_2',
}


def main():
    gltf, binary = read_glb(SOURCE)
    for node in gltf['nodes']:
        if node.get('name') in RENAME:
            node['name'] = RENAME[node['name']]
    export_texture(gltf, binary, TEXTURE)
    for key in ('images', 'textures', 'samplers', 'extensionsUsed', 'extensionsRequired'):
        gltf.pop(key, None)
    gltf['materials'] = [{'name': 'bear', 'doubleSided': True}]
    binary = compact(gltf, binary)
    write_glb(TARGET, gltf, binary)
    print('wrote', os.path.relpath(TARGET, ROOT), f'({os.path.getsize(TARGET) // 1024} KB) and',
          os.path.relpath(TEXTURE, ROOT), f'({os.path.getsize(TEXTURE) // 1024} KB)')


if __name__ == '__main__':
    main()
