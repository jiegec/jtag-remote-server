{ stdenv, cmake, libftdi1, pkg-config }:

stdenv.mkDerivation {
  name = "jtag-remote-server";
  version = "1.0";

  src = ./.;

  nativeBuildInputs = [
    cmake
  ];

  buildInputs = [
    libftdi1
    pkg-config
  ];
}
