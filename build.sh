cmake . -Bbuild -DSD_CUBLAS=ON -DCMAKE_CUDA_COMPILER=/usr/local/cuda-12/bin/nvcc -DCMAKE_CUDA_ARCHITECTURES=86
cmake --build build --config Release -j