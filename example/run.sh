# 分析 + 转换
opt-18 -load-pass-plugin=../build/advancedhbm/AdvancedHBMPlugin.so \
                                              -passes=hbm-transform \
                                              -hbm-report-file=report.json \
                                              -S source.ll -o optimized.ll

# 链接
clang++-18 optimized.ll -o program \
                        ../build/advancedhbm/libHBMMemoryManager.a \
                        -lmemkind -lpthread \
                        -Wl,--wrap=malloc -Wl,--wrap=free
