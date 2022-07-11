{
  "targets": [
    {
      "target_name": "spicy",
      "sources": [ "src/spicy-addon.cc" ],
      "include_dirs": [
        "/opt/spicy/include",
      ],
      "libraries": [
        "-L/opt/spicy/lib64",
        "-L/opt/spicy/lib",  # The binary ships lib, locally I have lib64
        "-Wl,--whole-archive",
        "-Wl,-rpath,/opt/spicy/lib64",
        "-Wl,-rpath,/opt/spicy/lib",
        "-lspicy-rt",
        "-lz",
        "-ldl",
        "-lhilti-rt",
        "-Wl,--no-whole-archive"
      ],
      "ldflags": [
        "-fPIC",
        "-Wl,--export-dynamic",
      ],
      "cflags_cc": [
        "-std=c++17",
        "-fPIC",
        "-Wno-deprecated-copy",
        "-fvisibility=hidden"
      ],
      "cflags_cc!": [
        "-fno-exceptions",
        "-fno-rtti"
      ],
    }
  ]
}
