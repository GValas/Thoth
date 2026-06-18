cmake -B build 
cmake --build build -j 
./build/thoth -batch ./samples/big_option.yaml ./samples/big_option.out.yaml
dot -Tpng ./samples/big_option_nodes.dot -o ./samples/big_option_nodes.png 