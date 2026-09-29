"""Bake user-supplied CC-BY GLBs into bounded coloured PSP meshes.

Textures become vertex colours; PBR normal/roughness maps are not reproduced.
Fully transparent faces are removed; remaining alpha is flattened to opaque.
Material-aware vertex clustering reduces oversized models without dropping
arbitrary triangles. Originals and attribution remain alongside the conversion.
"""
import io
import json
from pathlib import Path
import struct
import sys
import numpy as np
from PIL import Image


def load(path):
    raw = path.read_bytes()
    if struct.unpack_from('<III', raw) != (0x46546c67, 2, len(raw)):
        raise ValueError(f'{path}: invalid GLB')
    chunks = {}; offset = 12
    while offset < len(raw):
        size, kind = struct.unpack_from('<II', raw, offset)
        chunks[kind] = raw[offset+8:offset+8+size]; offset += size+8
    doc = json.loads(chunks[0x4e4f534a]); binary = chunks[0x004e4942]
    if any(doc.get(k) for k in ('animations', 'skins', 'extensionsRequired')):
        raise ValueError('Animated/skinned/required-extension mesh unsupported')
    def access(index):
        a = doc['accessors'][index]; v = doc['bufferViews'][a['bufferView']]
        if a.get('sparse') or a.get('normalized') or v.get('buffer', 0):
            raise ValueError('Unsupported accessor')
        dtype = np.dtype({5126:'<f4',5125:'<u4',5123:'<u2',5121:'u1'}[a['componentType']])
        width = {'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4}[a['type']]
        return np.ndarray((a['count'], width), dtype, binary,
                          v.get('byteOffset',0)+a.get('byteOffset',0),
                          (v.get('byteStride',dtype.itemsize*width),dtype.itemsize)).copy()
    images = {}
    def image(index):
        if index not in images:
            entry = doc['images'][doc['textures'][index]['source']]
            view = doc['bufferViews'][entry['bufferView']]
            data = binary[view.get('byteOffset',0):view.get('byteOffset',0)+view['byteLength']]
            images[index] = np.asarray(Image.open(io.BytesIO(data)).convert('RGBA'))/255.
        return images[index]
    result = []
    def visit(index, parent, seen):
        if index in seen: raise ValueError('Cyclic nodes')
        node = doc['nodes'][index]
        if any(k in node for k in ('translation','rotation','scale')):
            raise ValueError('Expected supplied matrix transforms')
        matrix = parent @ np.array(node.get('matrix',np.eye(4).flatten(order='F')),float).reshape(4,4,order='F')
        if 'mesh' in node:
            for prim in doc['meshes'][node['mesh']]['primitives']:
                if prim.get('mode',4)!=4 or prim.get('targets'): raise ValueError('Expected static triangles')
                positions = access(prim['attributes']['POSITION']).astype(float)
                positions = np.c_[positions,np.ones(len(positions))] @ matrix.T
                normals = access(prim['attributes']['NORMAL']) @ np.linalg.inv(matrix[:3,:3])
                normals /= np.maximum(np.linalg.norm(normals,axis=1)[:,None],1e-12)
                material = doc['materials'][prim['material']]; pbr = material.get('pbrMetallicRoughness',{})
                colours = np.tile(pbr.get('baseColorFactor',[1,1,1,1]),(len(positions),1)).astype(float)
                if 'baseColorTexture' in pbr:
                    tex = image(pbr['baseColorTexture']['index']); uv = access(prim['attributes']['TEXCOORD_0'])
                    # glTF UV origin matches decoded image row order. Repeat default.
                    x = (np.mod(uv[:,0],1)*tex.shape[1]).astype(int)
                    y = (np.mod(uv[:,1],1)*tex.shape[0]).astype(int)
                    sample = tex[y,x].copy()
                    sample[:,:3] = np.where(sample[:,:3]<=.04045,sample[:,:3]/12.92,((sample[:,:3]+.055)/1.055)**2.4)
                    colours *= sample
                emission = np.tile(material.get('emissiveFactor',[0,0,0]),(len(positions),1)).astype(float)
                if 'emissiveTexture' in material:
                    tex = image(material['emissiveTexture']['index']); uv = access(prim['attributes']['TEXCOORD_0'])
                    sample=tex[(np.mod(uv[:,1],1)*tex.shape[0]).astype(int),(np.mod(uv[:,0],1)*tex.shape[1]).astype(int),:3]
                    emission *= np.where(sample<=.04045,sample/12.92,((sample+.055)/1.055)**2.4)
                positions[:,[0,2]] *= -1; normals[:,[0,2]] *= -1
                light = .45+.55*np.maximum(0,normals@np.array([-.3,.8,-.52]))
                colours[:,:3] = np.clip(colours[:,:3]*light[:,None]+emission,0,1)
                colours[:,:3] = np.where(colours[:,:3]<=.0031308,colours[:,:3]*12.92,1.055*colours[:,:3]**(1/2.4)-.055)
                indices = access(prim['indices']).flatten()
                for face in indices.reshape(-1,3):
                    if max(colours[face,3])<.01:continue
                    result.append((positions[face,:3],colours[face,:3],prim['material']))
        for child in node.get('children',[]):visit(child,matrix,seen|{index})
    for root in doc['scenes'][doc.get('scene',0)]['nodes']:visit(root,np.eye(4),set())
    points = np.array([v for v,c,m in result]); colours = np.array([c for v,c,m in result])
    materials = np.array([m for v,c,m in result]); low=points.min(axis=(0,1)); high=points.max(axis=(0,1))
    if not np.isfinite(points).all() or max(high-low)<=0: raise ValueError('Invalid mesh geometry')
    points = (points-(low+high)/2)*(.9/max(high-low))
    return doc, points, colours, materials


def simplify(points, colours, materials, budget):
    if len(points)<=budget:return points,colours
    # Every cluster retains its mean position and colour. Material seams never
    # merge; degenerate and duplicate faces are removed deterministically.
    for cell in np.geomspace(.003,.2,100):
        keys = np.c_[np.rint(points.reshape(-1,3)/cell).astype(int),np.repeat(materials,3)]
        _, inverse = np.unique(keys,axis=0,return_inverse=True)
        count = np.bincount(inverse); verts=np.zeros((len(count),3)); rgb=verts.copy()
        np.add.at(verts,inverse,points.reshape(-1,3));np.add.at(rgb,inverse,colours.reshape(-1,3))
        verts/=count[:,None];rgb/=count[:,None];faces=inverse.reshape(-1,3)
        valid=(faces[:,0]!=faces[:,1])&(faces[:,1]!=faces[:,2])&(faces[:,0]!=faces[:,2])
        faces=faces[valid];_, unique=np.unique(np.sort(faces,axis=1),axis=0,return_index=True);faces=faces[np.sort(unique)]
        if len(faces)<=budget:
            return verts[faces],rgb[faces]
    raise ValueError('Cannot fit model budget')


def main(folder, target):
    rows=['/* Generated by tools/import_cave_models.py; see assets/ships/CREDITS.md. */']
    info=[]; credits=['# Imported flight models\n\nOriginal GLBs retain CC BY 4.0, independently of code licensing.\n']
    report=[]
    for index,name in enumerate([f'Low_Poly_{i}.glb' for i in range(1,8)]+['turret.glb']):
        doc, points, colours, materials=load(folder/name); original=len(points)
        points,colours=simplify(points,colours,materials,700 if index==7 else 600)
        ext=np.max(np.abs(points),axis=(0,1)); info.append((len(points)*3,ext))
        rows.append(f'static const MdVertex cave_model_{index}[] = {{')
        for p,c in zip(points.reshape(-1,3),colours.reshape(-1,3)):
            rgb=np.rint(c*255).astype(int);colour=0xff000000|int(rgb[0])|int(rgb[1])<<8|int(rgb[2])<<16
            rows.append(' {0,0,0x%08x,%s},'%(colour,','.join(f'{v:.7f}f' for v in p)))
        rows.append('};')
        extra=doc['asset'].get('extras',{});credits.append(f"## {name}\n\n{extra.get('title')} — {extra.get('author')}\n\n{extra.get('source')}\n\nLicense: {extra.get('license')}\n\nPSP: {original} → {len(points)} triangles; transforms/lighting baked, texture base colours sampled at vertices; normal/metallic maps omitted. Fully transparent faces removed, remaining alpha flattened to opaque.\n")
        report.append(f'{name}: {original} -> {len(points)} triangles, extents {ext}')
    rows.append('static const CaveModel cave_models[] = {')
    for i,(n,ext) in enumerate(info):rows.append(' {cave_model_%d,%d,{%s}},'%(i,n,','.join(f'{v:.7f}f' for v in ext)))
    rows.append('};\n')
    target.write_text('\n'.join(rows));(folder/'CREDITS.md').write_text('\n'.join(credits))
    target.with_name('cave_models_bounds.h').write_text(
        '/* Generated model half-extents; shared collision/placement metadata. */\n'
        'static const float cave_model_half[][3] = {\n'+
        '\n'.join(' {'+','.join(f'{v:.7f}f' for v in ext)+'},' for _,ext in info)+'\n};\n')
    print('\n'.join(report))

if __name__=='__main__':main(Path(sys.argv[1]),Path(sys.argv[2]))
