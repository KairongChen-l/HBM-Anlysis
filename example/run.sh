# 分析 + 转换
opt-18 -load-pass-plugin=../build/advancedhbm/AdvancedHBMPlugin.so \
                                              -passes=hbm-transform \
                                              -hbm-report-file=report.json \
                                              -S source.ll -o optimized.ll

# 链接
clang optimized.ll -o program \
                        ../build/advancedhbm/libHBMMemoryManager.so \
                        -lmemkind -lpthread \
                        -Wl,--wrap=malloc -Wl,--wrap=free


# 绝对路径
opt-18 -load-pass-plugin=/home/dell/space/HBM-Anlysis/build/advancedhbm/AdvancedHBMPlugin.so \
                                              -passes=hbm-transform \
                                              -hbm-report-file=report.json \
                                              -S source.ll -o optimized.ll


# 非侵入性
clang-18 -flto=thin -O2 \
  -fuse-ld=lld \
  -fpass-plugin=/home/dell/space/HBM-Anlysis/build/advancedhbm/AdvancedHBMPlugin.so \
  main.c memory_utils.c -o main


clang-18 -flto -O2   -fuse-ld=lld \
  -fpass-plugin=/home/dell/space/HBM-Anlysis/build/advancedhbm/AdvancedHBMPlugin.so \
     *.c /home/dell/space/HBM-Anlysis/build/advancedhbm/libHBMMemoryManager.a  \
      -Wl,--wrap=malloc -Wl,--wrap=free \
        -lmemkind -lpthread  \
         -lstdc++ -lc++abi -lgcc -lm -o main

clang-18 -flto=thin -O2   -fuse-ld=lld \
  -fpass-plugin=/home/dell/space/HBM-Anlysis/build/advancedhbm/AdvancedHBMPlugin.so \
     *.c /home/dell/space/HBM-Anlysis/build/advancedhbm/libHBMMemoryManager.a  \
      -Wl,--wrap=malloc -Wl,--wrap=free \
        -lmemkind -lpthread  \
         -lstdc++ -lc++abi -lgcc -lm -o main



-I/opt/memkind/include         # 添加头文件搜索路径
-L/opt/memkind/lib             # 添加库文件搜索路径
-lmemkind                      # 链接 memkind 库

export PKG_CONFIG_PATH=/opt/memkind/lib/pkgconfig:$PKG_CONFIG_PATH
pkg-config --cflags --libs memkind


export LD_LIBRARY_PATH=/opt/memkind/lib:$LD_LIBRARY_PATH
echo "/opt/memkind/lib" | sudo tee /etc/ld.so.conf.d/memkind.conf
sudo ldconfig


 clang source.ll -o program  ../build/advancedhbm/libHBMMemoryManager.so  -lmemkind -ldl -lpthread