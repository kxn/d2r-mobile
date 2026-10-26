// Offline wrapper around the unmodified, source-pinned upstream passes.
#include "xbg_spirv_quad.h"
#include "xbg_spirv_ldc.h"
#include "xbg_spirv_temps.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc,char **argv) {
    try {
        if(argc!=5)throw std::runtime_error("input output quad(0/1) constants(0/1)");
        const std::string quad=argv[3],constants=argv[4];
        if((quad!="0" && quad!="1") || (constants!="0" && constants!="1"))
            throw std::runtime_error("expected boolean rewrite options");
        std::ifstream in(argv[1],std::ios::binary|std::ios::ate);
        auto size=in.tellg();if(size<20 || size%4)throw std::runtime_error("invalid SPIR-V size");
        std::vector<uint32_t> words(size_t(size)/4),out;
        in.seekg(0);in.read(reinterpret_cast<char*>(words.data()),size);
        if(!in || words[0]!=0x07230203)throw std::runtime_error("invalid SPIR-V input");
        size_t q=0,c=0,t=0;xbg_spirv_quad_stats stats{};
        if(quad=="1") {q=xbg_spirv_quad_derivatives(words.data(),words.size(),out,&stats);if(q)words.swap(out);}
        if(constants=="1") {
            c=xbg_spirv_ldc_fold(words.data(),words.size(),out);if(c)words.swap(out);
            t=xbg_spirv_forward_temps(words.data(),words.size(),out);if(t)words.swap(out);
        }
        std::ofstream file(argv[2],std::ios::binary|std::ios::trunc);
        file.write(reinterpret_cast<const char*>(words.data()),words.size()*4);
        file.close();if(!file)throw std::runtime_error("SPIR-V output write failed");
        std::cout<<"{\"quad_rewrites\":"<<q<<",\"quad_candidates\":"<<stats.candidates
                 <<",\"quad_helper_candidates\":"<<stats.calls<<",\"quad_helpers_rewritten\":"<<stats.calls_rewritten
                 <<",\"component_folds\":"<<c<<",\"temps_forwarded\":"<<t<<"}\n";
        return 0;
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
