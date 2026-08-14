# Test to see if we can access the EC2 metadata server. And cache that result.
# If we can access it, assume we're running on REDACTED ci with access to
# sccache.
if [[ -f "/opt/galois-llvm-build/share/sccache.env" ]]; then
  if ! [[ -f "/tmp/bracelet-ci-dont-use-sccache.txt" ]]; then
    if ! [[ -f "/tmp/bracelet-ci-use-sccache.txt" ]]; then
      if timeout 1 curl --verbose http://169.254.169.254/latest/meta-data/ > /dev/null; then
        echo 1 > "/tmp/bracelet-ci-use-sccache.txt"
      else
        echo 1 > "/tmp/bracelet-ci-dont-use-sccache.txt"
      fi
    fi
    if [[ -f "/tmp/bracelet-ci-use-sccache.txt" ]]; then
      export SCCACHE_IDLE_TIMEOUT=0
      source /opt/galois-llvm-build/share/sccache.env
      export SCCACHE_S3_KEY_PREFIX="$SCCACHE_S3_KEY_PREFIX/downstream/$(cat /opt/bracelet-llvm/llvm_image.txt)"
      launcher=(/opt/galois-llvm-build/bin/sccache)
    fi
  fi
fi
