#include <cassert>
#include <cstdio>
#include "yuzu_shader_recompiler/ir_opt/bindless_descriptor.h"
int main(){unsigned tests=0;for(unsigned size=0;size<128;++size)for(unsigned offset=0;offset<140;++offset)for(unsigned stride=0;stride<65;++stride){unsigned expected=0;if(stride)for(unsigned pos=offset;pos<=size && size-pos>=4;pos+=stride)++expected;assert(Shader::Optimization::BindlessDescriptorCountStrided(size,offset,stride)==expected);++tests;}assert(Shader::Optimization::BindlessDescriptorCountStrided(~0u,~0u,4)==0);printf("strided descriptor bounds: %u cases PASS\n",tests);}
