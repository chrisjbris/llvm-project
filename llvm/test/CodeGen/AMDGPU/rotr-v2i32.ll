; RUN: llc -mtriple=amdgcn -verify-machineinstrs  -debug-only=isel  %s -o - 2>&1 | FileCheck -check-prefixes=DEBUG,SI %s
; REQUIRES: asserts

; DEBUG-LABEL: Optimized legalized selection DAG: %bb.0 'rotr_v2i32:entry'
; DEBUG: t[[V1:[0-9]+]]: v2i32 = BUILD_VECTOR t{{[0-9]+}}, t{{[0-9]+}}
; DEBUG: t[[V2:[0-9]+]]: v2i32 = BUILD_VECTOR t{{[0-9]+}}, t{{[0-9]+}}
; DEBUG: t{{[0-9]+}}: v2i32 = rotr t[[V1]], t[[V2]]

; SI-LABEL: rotr_v2i32:
; SI:       ; %bb.0: ; %entry
; SI-NEXT:    s_load_dwordx4 s[0:3], s[4:5], 0xb
; SI-NEXT:    s_load_dwordx2 s[4:5], s[4:5], 0x9
; SI-NEXT:    s_mov_b32 s7, 0xf000
; SI-NEXT:    s_mov_b32 s6, -1
; SI-NEXT:    s_waitcnt lgkmcnt(0)
; SI-NEXT:    v_mov_b32_e32 v0, s3
; SI-NEXT:    v_alignbit_b32 v1, s1, s1, v0
; SI-NEXT:    v_mov_b32_e32 v0, s2
; SI-NEXT:    v_alignbit_b32 v0, s0, s0, v0
; SI-NEXT:    buffer_store_dwordx2 v[0:1], off, s[4:7], 0
; SI-NEXT:    s_endpgm

define amdgpu_kernel void @rotr_v2i32(ptr addrspace(1) %in, <2 x i32> %x, <2 x i32> %y) {

entry:
  %tmp0 = sub <2 x i32> <i32 32, i32 32>, %y
  %tmp1 = shl <2 x i32> %x, %tmp0
  %tmp2 = lshr <2 x i32> %x, %y
  %tmp3 = or <2 x i32> %tmp1, %tmp2
  store <2 x i32> %tmp3, ptr addrspace(1) %in
  ret void
}
