#include "collide_drawable.h"

#include "common/util/Assert.h"

#include "goalc/data_compiler/DataObjectGenerator.h"

/*
(deftype drawable (basic)
  ((id      int16          :offset-assert 4)
   (bsphere vector :inline :offset-assert 16)
   )

(deftype drawable-group (drawable)
  ((length  int16       :offset 6)
   (data    drawable 1  :offset-assert 32)
   )

(deftype drawable-tree (drawable-group)
  ()

(deftype drawable-inline-array (drawable)
  ((length  int16          :offset 6) ;; this is kinda weird.
   )

(deftype drawable-tree-collide-fragment (drawable-tree)
  ((data-override drawable-inline-array 1 :offset 32)) ;; should be 1 there

(deftype drawable-inline-array-collide-fragment (drawable-inline-array)
  ((data    collide-fragment 1 :inline      :offset-assert 32)

 (deftype collide-fragment (drawable)
  ((mesh    collide-frag-mesh          :offset 8)
   )
  :method-count-assert 18
  :size-assert         #x20

(deftype collide-frag-mesh (basic)
  ((packed-data     uint32         :offset-assert 4)
   (pat-array       uint32         :offset-assert 8)
   (strip-data-len  uint16         :offset-assert 12)
   (poly-count      uint16         :offset-assert 14)
   (base-trans      vector :inline :offset-assert 16)
   ;; these go in the w of the vector above.
   (vertex-count    uint8          :offset 28)
   (vertex-data-qwc uint8          :offset 29)
   (total-qwc       uint8          :offset 30)
   (unused          uint8          :offset 31)
   )
  :method-count-assert 9
  :size-assert         #x20

(deftype draw-node (drawable)
  ((child-count uint8          :offset 6)
   (flags       uint8          :offset 7)
   (child       drawable        :offset 8)
   (distance    float          :offset 12)
   )
   :size-assert         #x20

 (deftype drawable-inline-array-node (drawable-inline-array)
  ((data draw-node 1 :inline)
   (pad uint32)
   )
  :method-count-assert 18
  :size-assert         #x44
 */

size_t generate_pat_array(DataObjectGenerator& gen, const std::vector<PatSurface>& pats) {
  gen.align_to_basic();
  size_t result = gen.current_offset_bytes();
  for (auto& pat : pats) {
    gen.add_word(pat.val);
  }
  return result;
}

size_t generate_packed_collide_data(DataObjectGenerator& gen, const std::vector<u8>& data) {
  gen.align_to_basic();
  size_t result = gen.current_offset_bytes();
  ASSERT((data.size() % 4) == 0);
  for (size_t i = 0; i < data.size(); i += 4) {
    u32 word;
    memcpy(&word, data.data() + i, 4);
    gen.add_word(word);
  }
  return result;
}

size_t generate_collide_frag_mesh(DataObjectGenerator& gen,
                                  const CollideFragMeshData& mesh,
                                  size_t packed_data_loc,
                                  size_t pat_array_loc) {
  gen.align_to_basic();
  gen.add_type_tag("collide-frag-mesh");  // 0
  size_t result = gen.current_offset_bytes();
  gen.link_word_to_byte(gen.add_word(0), packed_data_loc);      // 4
  gen.link_word_to_byte(gen.add_word(0), pat_array_loc);        // 8
  gen.add_word(mesh.strip_data_len | (mesh.poly_count << 16));  // 12
  gen.add_word(mesh.base_trans_xyz_s32.x());                    // 16
  gen.add_word(mesh.base_trans_xyz_s32.y());                    // 20
  gen.add_word(mesh.base_trans_xyz_s32.z());                    // 24
  u32 packed = 0;
  packed |= mesh.vertex_count;
  packed |= ((u32)mesh.vertex_data_qwc) << 8;
  packed |= ((u32)mesh.total_qwc) << 16;
  gen.add_word(packed);  // 28
  return result;
}

size_t generate_collide_fragment(DataObjectGenerator& gen,
                                 const CollideFragMeshData& mesh,
                                 size_t frag_mesh_loc) {
  /*
    .type collide-fragment
    .word 0x10000
    .word L705
    .word 0x0
    .word 0x46bf480a
    .word 0x43dc730b
    .word 0xb71ed4fe
    .word 0x46c42e44
   */
  gen.align_to_basic();
  gen.add_type_tag("collide-fragment");
  size_t result = gen.current_offset_bytes();
  gen.add_word(0x10000);  // ???
  gen.link_word_to_byte(gen.add_word(0), frag_mesh_loc);
  gen.add_word(0);
  for (int i = 0; i < 4; i++) {
    gen.add_word_float(mesh.bsphere[i]);
  }

  return result;
}

size_t generate_collide_fragment_array(DataObjectGenerator& gen,
                                       const std::vector<CollideFragMeshData>& meshes,
                                       const std::vector<size_t>& frag_mesh_locs,
                                       std::vector<size_t>& parent_ref_out) {
  gen.align_to_basic();
  gen.add_type_tag("drawable-inline-array-collide-fragment");  // 0
  size_t result = gen.current_offset_bytes();
  ASSERT(meshes.size() < UINT16_MAX);
  gen.add_word(meshes.size() << 16);  // 4, 6
  gen.add_word(0);                    // 8
  gen.add_word(0);                    // 12
  gen.add_word(0);                    // 16
  gen.add_word(0);                    // 20
  gen.add_word(0);                    // 24
  gen.add_word(0);                    // 28

  ASSERT(meshes.size() == frag_mesh_locs.size());
  for (size_t i = 0; i < meshes.size(); i++) {
    auto& mesh = meshes[i];
    // should be 8 words here:
    gen.add_type_tag("collide-fragment");  // 1
    size_t me = gen.current_offset_bytes();
    gen.add_word(0x10000);  // ???
    gen.link_word_to_byte(gen.add_word(0), frag_mesh_locs[i]);
    gen.add_word(0);
    for (int j = 0; j < 4; j++) {
      gen.add_word_float(mesh.bsphere[j]);
    }
    if ((i % 8) == 0) {
      parent_ref_out.push_back(me);
    }
  }

  return result;
}

size_t generate_collide_draw_node_array(DataObjectGenerator& gen,
                                        const std::vector<collide::DrawNode>& nodes,
                                        u32 flag,
                                        const std::vector<size_t>& children,
                                        std::vector<size_t>& parent_ref_out) {
  gen.align_to_basic();
  gen.add_type_tag("drawable-inline-array-node");  // 0
  size_t result = gen.current_offset_bytes();
  gen.add_word(nodes.size() << 16);  // 4, 6
  gen.add_word(0);                   // 8
  gen.add_word(0);                   // 12
  gen.add_word(0);                   // 16
  gen.add_word(0);                   // 20
  gen.add_word(0);                   // 24
  gen.add_word(0);                   // 28

  ASSERT(nodes.size() == children.size());
  for (size_t i = 0; i < nodes.size(); i++) {
    auto& node = nodes[i];
    // should be 8 words here:
    gen.add_type_tag("draw-node");  // 1
    size_t me = gen.current_offset_bytes();
    u32 packed_flags = 0;
    packed_flags |= (8 << 16);  // TODO hard-coded size here
    packed_flags |= (flag << 24);
    gen.add_word(packed_flags);                           // 2
    gen.link_word_to_byte(gen.add_word(0), children[i]);  // 3
    gen.add_word(0);                                      // 4
    if ((i % 8) == 0) {
      parent_ref_out.push_back(me);
    }
    gen.add_word_float(node.bsphere.x());  // 5
    gen.add_word_float(node.bsphere.y());  // 6
    gen.add_word_float(node.bsphere.z());  // 7
    gen.add_word_float(node.bsphere.w());  // 8
  }

  return result;
}

size_t DrawableTreeCollideFragment::add_to_object_file(DataObjectGenerator& gen) const {
  // generate pat array
  size_t pat_array_loc = generate_pat_array(gen, packed_frags.pats);

  // generated packed data
  std::vector<size_t> packed_data_locs;
  for (auto& mesh : packed_frags.packed_frag_data) {
    packed_data_locs.push_back(generate_packed_collide_data(gen, mesh.packed_data));
  }

  // generate collide frag meshes
  std::vector<size_t> collide_frag_meshes;
  for (size_t i = 0; i < packed_data_locs.size(); i++) {
    collide_frag_meshes.push_back(generate_collide_frag_mesh(gen, packed_frags.packed_frag_data[i],
                                                             packed_data_locs[i], pat_array_loc));
  }

  std::vector<size_t> array_locs;
  array_locs.resize(bvh.node_arrays.size() + 1);  // plus one for the frags.
  int array_slot = bvh.node_arrays.size();

  std::vector<size_t> children_refs;
  array_locs[array_slot--] = generate_collide_fragment_array(gen, packed_frags.packed_frag_data,
                                                             collide_frag_meshes, children_refs);
  u32 flag = 0;
  while (array_slot >= 0) {
    ASSERT(children_refs.size() == bvh.node_arrays.at(array_slot).nodes.size());
    std::vector<size_t> next_children;

    array_locs[array_slot] = generate_collide_draw_node_array(
        gen, bvh.node_arrays.at(array_slot).nodes, flag, children_refs, next_children);

    children_refs = std::move(next_children);
    array_slot--;
    flag = 1;
  }

  {
    gen.align_to_basic();
    gen.add_type_tag("drawable-tree-collide-fragment");
    size_t result = gen.current_offset_bytes();
    gen.add_word((array_locs.size() - 1) << 16);  // todo the minus one here??
    for (int i = 0; i < 6; i++) {
      gen.add_word(0);
    }

    for (size_t i = 1; i < array_locs.size(); i++) {  // todo the offset here?
      gen.link_word_to_byte(gen.add_word(0), array_locs[i]);
    }

    return result;
  }
}
