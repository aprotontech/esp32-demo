# esp32-demo

# 编译

## Window 环境

### ESP-IDF 环境安装

可以参考网上文档，这里省略

### 初始化

```
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=%IDF_PATH%/tools/cmake/toolchain-esp32.cmake -DTARGET=esp32 -GNinja
```

### 编译

```
idf.py build
```

### 烧录

```
idf.py -p COM4 flash
```
