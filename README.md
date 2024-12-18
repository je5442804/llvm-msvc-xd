# `llvm-msvc`



### add Something for ollvm
```
Original obfuscation passes including bogus control flow (-bcf), split basic block (-split), control flow flattening (-fla) and substitution (-sub) and MBA-substitution(-mba-subs) and Indirect-Call(-ind-call) and StringXor(-string-obfus) and ConstVarXor(-const-obfus) and VM-Flattening (-vm-fla).
``` 

### 感谢
```
https://github.com/gmh5225, 日天同学的llvm-msvc让人心情激动

```
### 参考
```
https://github.com/gmh5225/awesome-llvm-security#ollvm
```

### 更新
放弃TAG自动Build了，自己build吧

### 计划
- [x] 在vm-fla-sym添加反符号执行和反内存追踪
- [x] vm-fla-enc 对vm-fla的部分数据加密
- ~~[ ] MBA-subs的bug~~
- ~~[ ] 移植xVMP~~
- [x] 在vm-fla-enc中使用间接全局变量访问
- [x] vm-fla-level 0~7 8个处理等级 7最强，0最弱，默认7
- [x] 弱鸡vmp加入 
- [x] 添加combine功能
- [x] 添加fla強化 x-fla-enh
- [x] x-full 功能，在function上使用vm-fla-level=7
- [x] 字符串加密等相似加了combine
- [x] 自定義分割合併 combine_func[tag_number] 模式
- ~~[ ] x-var-rot 待处理~~
- [x] 新功能
- [x] custom-cc 参数传递和返回值的方法
- [ ] new functions


[![windows-llvm-msvc-build](https://github.com/backengineering/llvm-msvc/actions/workflows/windows-llvm-msvc-build.yml/badge.svg?branch=dev)](https://github.com/backengineering/llvm-msvc/actions/workflows/windows-llvm-msvc-build.yml)
[![android-llvm-msvc-build](https://github.com/backengineering/llvm-msvc/actions/workflows/android-llvm-msvc-build.yml/badge.svg?branch=dev)](https://github.com/backengineering/llvm-msvc/actions/workflows/android-llvm-msvc-build.yml)
[![macos-arm64-llvm-msvc-build](https://github.com/backengineering/llvm-msvc/actions/workflows/macos-arm64-llvm-msvc-build.yml/badge.svg)](https://github.com/backengineering/llvm-msvc/actions/workflows/macos-arm64-llvm-msvc-build.yml)

![image](https://github.com/backengineering/llvm-msvc/assets/13917777/86a7eb6a-466a-4290-8ec5-e3affc3e3c0a)
[![Github All Releases](https://img.shields.io/github/downloads/backengineering/llvm-msvc/total.svg)](https://github.com/backengineering/llvm-msvc/releases) 
[![GitHub release](https://img.shields.io/github/release/backengineering/llvm-msvc.svg)](https://github.com/backengineering/llvm-msvc/releases) 

``llvm-msvc`` is a compiler based on ``LLVM`` that isn't limited by ``MSVC``. The aim is to provide the same experience as ``MSVC`` on Windows. You can use naked functions anywhere and also add custom support like obfuscation.

```
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⣀⣀⣀⣀⣀⣠⣼⠂⠀⠀⠀⠀⠙⣦⢀⠀⠀⠀⠀⠀⢶⣤⣀⣀⣀⣀⣀⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⣠⣴⣶⣿⣿⣿⣿⣿⣿⣿⣿⠷⢦⠀⣹⣶⣿⣦⣿⡘⣇⠀⠀⠀⢰⠾⣿⣿⣿⣟⣻⣿⣿⣿⣷⣦⣄⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⣤⣾⣿⣿⣿⣿⣿⣿⣿⣿⣿⡟⠀⠀⠀⠀⢺⣿⣿⣿⣿⣿⣿⣿⣆⠀⠀⠀⠀⠀⠀⢹⣿⣿⣿⣿⣿⣿⣿⣿⣿⣷⣦⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⢀⣴⢟⣥⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀⢻⣿⣿⡏⢹⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣮⣝⢷⣄⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⣴⣿⣿⣿⣿⣿⣿⣿⣿⣿⡿⢛⣿⣿⣿⡇⠀⠀⠀⠀⠛⣿⣿⣷⡀⠘⢿⣧⣻⡷⠀⠀⠀⠀⠀⠀⣿⣿⣿⣟⢿⣿⣿⣿⣿⣿⣿⣿⣿⣝⢧⡀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⢠⣾⣿⠟⣡⣾⣿⣿⣧⣿⡿⣋⣴⣿⣿⣿⣿⣧⠀⠀⠀⠀⠀⢻⣿⣿⣿⣶⡄⠙⠛⠁⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣷⣝⢻⣿⣟⣿⣿⣷⣮⡙⢿⣽⣆⠀⠀⠀⠀⠀
⠀⠀⠀⠀⢀⡿⢋⣴⣿⣿⣿⣿⣿⣼⣯⣾⣿⣿⡿⣻⣿⣿⣿⣦⠀⠀⠀⠀⢀⣹⣿⣿⣿⣿⣶⣤⠀⠀⠀⠀⠀⣰⣿⣿⣿⣿⠻⣿⣿⣿⣮⣿⣿⣿⣿⣿⣿⣦⡙⢿⣇⠀⠀⠀⠀
⠀⠀⠀⣠⡏⣰⣿⣿⡿⢿⣿⣿⣿⣿⣿⣿⡿⢋⣼⣿⣿⣿⣿⣿⣷⡤⠀⣠⣿⣿⣿⣿⣿⣿⣿⣿⣷⣄⠀⢠⣾⣿⣿⣿⣿⣿⣷⡜⢿⣿⣿⣿⣿⣿⣿⡿⠿⣿⣿⣦⡙⣦⠀⠀⠀
⠀⠀⣰⢿⣿⣿⠟⠋⣠⣾⣿⣿⣿⣿⣿⠛⢡⣾⡿⢻⣿⣿⣿⣿⣿⣿⣿⣿⡿⠋⠻⣿⡟⣿⣿⣿⠻⢿⣿⣿⣿⣿⣿⣿⣿⣟⠻⣿⣆⠙⢿⣿⣿⣿⣿⣿⣦⡈⠻⣿⣿⣟⣧⠀⠀
⠀⣰⢣⣿⡿⠃⣠⡾⠟⠁⠀⣸⣿⡟⠁⢀⣿⠋⢠⣿⡏⣿⣿⣿⣿⣿⢿⠁⢀⣠⣴⢿⣷⣿⣿⣿⠀⠀⠽⢻⣿⣿⣿⣿⡼⣿⡇⠈⢿⡆⠀⠻⣿⣧⠀⠈⠙⢿⣆⠈⠻⣿⣎⢧⠀
⠀⢣⣿⠟⢀⡼⠋⠀⠀⢀⣴⠿⠋⠀⠀⣾⡟⠀⢸⣿⠙⣿⠃⠘⢿⡟⠀⣰⢻⠟⠻⣿⣿⣿⣿⣿⣀⠀⠀⠘⣿⠋⠀⣿⡇⣿⡇⠀⠸⣿⡄⠀⠈⠻⣷⣄⠀⠀⠙⢷⡀⠙⣿⣆⠁
⢀⣿⡏⠀⡞⠁⢀⡠⠞⠋⠁⠀⠀⠀⠈⠉⠀⠀⠀⠿⠀⠈⠀⠀⠀⠀⠀⣿⣿⣰⣾⣿⣿⣿⣿⣿⣿⣤⠀⠀⠀⠀⠀⠉⠀⠸⠃⠀⠀⠈⠋⠀⠀⠀⠀⠙⠳⢤⣀⠀⠹⡄⠘⣿⡄
⣸⡟⠀⣰⣿⠟⠋⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⠛⠿⠿⠿⠟⠁⠀⠹⣿⣷⡄⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⠻⣿⣧⠀⢹⣷
⣿⠃⢠⡿⠃⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢠⣄⣤⣀⠀⠀⣿⣿⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⢻⡇⠀⣿
⣿⠀⢸⠅⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣰⡿⠋⠉⢻⣧⢀⣿⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣿⠀⢸
⡇⠀⠈⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢿⣧⡀⠀⠀⣿⣾⡟⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⠀⢸
⢸⡄⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⠻⠿⣿⣿⠟⠋⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡾
⠈⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣰⡿⠋⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠃
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢰⡏⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠘⣧⢀⣾⣤⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣀⡼⣿⣿⣾⣤⣠⡼⠀⠀⠀
```

## Features:
- Compatible with ``MSVC`` syntax as much as possible.
- Improved ``SEH`` stability.
- Added some special Intrinsic functions(``__vmx_vmread``/``__vmx_write``).
- Supports ``x64``/``ARM64`` windows drivers.
- Supports ``AArch64`` android GKI drivers.
- Allows naked ``X64`` inline asm.
- Enables multiple cores compilation.
- Supports ``/MP`` when precompiled headers are present.
- Supports ``/GL`` (LTO optimization).


## FAQ
### Why do we make this project?
- ``Clang`` follows the ``GCC`` standard, while ``MSVC`` has its own unique syntax.
- Some of the code is pretty hacky. Can't submit it officially.
- Waiting for the official fix takes too long.

### How to use llvm-msvc?
- Install Visual Studio 2015-2022 (I recommend using 2022) along with WDK11.
- Download and install llvm-msvc from the following repository [llvm-msvc_X86_64_installer-PDB.exe](https://github.com/backengineering/llvm-msvc/releases).
- Create your Windows driver project and choose "LLVM-MSVC_v143_KernelMode" as your platform toolset.
- Compile your project.

  
### How to compile?

```batch

X86：clang+lld+RelWithDebInfo

mkdir build-RelWithDebInfo-64
pushd build-RelWithDebInfo-64
cmake .. -G "Visual Studio 17 2022" -A X64 -DCMAKE_CXX_FLAGS="/utf-8" -DCMAKE_C_FLAGS="/utf-8" -DLLVM_ENABLE_RPMALLOC=ON -DLLDB_ENABLE_PYTHON=OFF -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_ENABLE_PROJECTS="clang;lld;lldb" -DCMAKE_INSTALL_PREFIX=E:\llvm\install-RelWithDebInfo-64 -DLLVM_ENABLE_LIBXML2=OFF -DLLVM_ENABLE_ZLIB=OFF -DLLVM_TARGETS_TO_BUILD=X86 -DLLVM_OBFUSCATION_LINK_INTO_TOOLS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLLVM_USE_CRT_RELEASE=MT ../llvm
msbuild /m -p:Configuration=RelWithDebInfo INSTALL.vcxproj 

X86：clang+lld+release

mkdir build-release-64
pushd build-release-64
cmake .. -G "Visual Studio 17 2022" -A X64 -DCMAKE_CXX_FLAGS="/utf-8" -DCMAKE_C_FLAGS="/utf-8" -DLLVM_ENABLE_RPMALLOC=ON -DLLDB_ENABLE_PYTHON=OFF -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_ENABLE_PROJECTS="clang;lld;lldb" -DCMAKE_INSTALL_PREFIX=E:\llvm\install-release-64 -DLLVM_ENABLE_LIBXML2=OFF -DLLVM_ENABLE_ZLIB=OFF -DLLVM_TARGETS_TO_BUILD=X86 -DLLVM_OBFUSCATION_LINK_INTO_TOOLS=ON -DCMAKE_BUILD_TYPE=release -DLLVM_USE_CRT_RELEASE=MT ../llvm

msbuild /m -p:Configuration=release INSTALL.vcxproj 
```

### 混淆例子
Add To VS Project Compiler Cmdline

set /GL off

set /O2 on
#### 最大保护（文件将超过100MB）
```
-mllvm -data-obfus -mllvm -const-obfus -mllvm -string-obfus -mllvm -ind-call -mllvm -vm-fla -mllvm -fla -mllvm -sub -mllvm -sub_loop=1 -mllvm -split -mllvm -split_num=3 -mllvm -bcf -mllvm -bcf_loop=1 -mllvm -bcf_prob=40 -mllvm -vm-fla-level=7 -mllvm -x-fla-enh -mllvm -x-var-rot -mllvm -x-combine
```
#### 单纯使用特色部分（轻量模式 不加Light会导致文件飞升到10MB）
```
-mllvm -data-obfus -mllvm -const-obfus -mllvm -string-obfus -mllvm -ind-call -mllvm -vm-fla -mllvm -vm-fla-level=0 -mllvm -x-fla-enh -mllvm -x-combine -mllvm -x-linear
```
#### 需要修改载研究的部分
```

```

#### vm sample and x-full sample
```c++
[[clang::annotate("x-vm,x-full,x-cfg,custom-cc")]]
void crypt_func1(uint8_t *var,uint8_t*key,size_t var_size,size_t key_size){
    for(auto i=0;i<var_size;i++){
        var[i]^=key[i%key_size];
    }
}
[[clang::annotate("x-cfg,ind-br,alias-access,custom-cc")]]
void crypt_func2(uint8_t *var,uint8_t*key,size_t var_size,size_t key_size){
    for(auto i=0;i<var_size;i++){
        var[i]^=key[i%key_size];
    }
}
[[clang::annotate("x-cfg,x-vm,ind-br,alias-access,custom-cc")]]
void crypt_func3(uint8_t *var,uint8_t*key,size_t var_size,size_t key_size){
    for(auto i=0;i<var_size;i++){
        var[i]^=key[i%key_size];
    }
}
```



#### combine sample
```c++
[[clang::annotate("combine_func[tag1]")]]
int a1(int a, int b)
{
    printf("%d , %d\r\n", a, b);
    printf("%x\r\n", a ^ b);
    return a + b;
}

[[clang::annotate("combine_func[tag1]")]]
int a2(int a, int b)
{
    std::cout << "hello1" << std::endl;
    for (auto i = std::min(a, b);i < std::max(a, b);i++)
    {
        printf("%x,", i);
    }
    printf("\n");
   
    return a * b+ a1(a, b);
}

[[clang::annotate("combine_func[tag2]")]]
int a3(int a,int b)
{
    printf("%d , %d\r\n", a+1, b+2);
    printf("%x\r\n", a ^ b);
    return a + b+a^b+ a2(a, b);
}


```
### How to contribute?
- https://github.com/HyunCafe/contribute-practice
- https://docs.github.com/en/get-started/quickstart/contributing-to-projects

### How can I learn LLVM?
If you don't know how to learn ``LLVM``, you can check out this [repository](https://github.com/gmh5225/awesome-llvm-security) of mine.

### Can it run on linux?
Yes.

### Can it run on macos?
Yes.

## Credits
- ``LLVM``
- Some anonymous people


