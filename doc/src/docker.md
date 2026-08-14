# Building the Docker Image

The nix scripts provide a docker image based on Ubuntu 24.04 with the toolchain already installed. This image is intended
to be used as a base image for CI builders that use the BRACELET toolchain.

## Fetching the Ubuntu Layer

To build using an Ubuntu base layer in nix you first have to fetch the Docker image into the nix store.

The `nix-prefetch-docker` tool can be used to do this.

Run `nix run nixpkgs#nix-prefetch-docker -- --image-name ubuntu --image-tag 24.04 --image-digest sha256:d1e2e92c075e5ca139d51a140fff46f84315c0fdce203eab2807c7e495eff4f9`

to fetch the current image used in `default.nix`. If you would like to update the base image you can remove the image-digest and update the default.nix with the imageDigest and hash produced by the command.

## Clear the Rules Cache

Unfortunately, uv2nix etc will setup a workspace that copies everything in the python source directory to the nix store. This can result in copying cached compiled rules
to the nix store if you have used bracelet_scripts locally. These compiled binaries will reference your local nix-cache and fail to run if a user analyzes the same project
you did locally. To avoid this run: `rm -r src/bracelet_scripts/rules/*`

## Build the Image 

Now you can build the image with: `nix-build -A docker-image`

## Loading the Image

To save on disk space the nix script builds the image as a streamed image so the `result` from nix is a script that streams the image to stdout.

So you can load the image by `./result | docker load`

You can now re-tag `bracelet-toolchain` as some other tag to push it to the repository.