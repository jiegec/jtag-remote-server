{
  description = "jtag-remote-server";

  outputs = { self, nixpkgs }: rec {
    packages.x86_64-linux.jtag-remote-server = nixpkgs.legacyPackages.x86_64-linux.callPackage ./default.nix { };
    packages.aarch64-darwin.jtag-remote-server = nixpkgs.legacyPackages.aarch64-darwin.callPackage ./default.nix { };

    hydraJobs.jtag-remote-server.x86_64-linux = packages.x86_64-linux.jtag-remote-server;
  };
}
