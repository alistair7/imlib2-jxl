#!/bin/sh
# Very dumb script that tries to install jxl.so to imlib2's loaders directory.

if [ ! -f jxl.so ]; then
  echo "Run make first" >&2
  exit 1
fi

for DIR in "/usr/lib/$HOSTTYPE-$OSTYPE/imlib2/loaders" /usr/lib/x86_64-linux-gnu/imlib2/loaders /usr/lib/imlib2/loaders; do
  if [ -d "$DIR" ]; then
    if install -m755 jxl.so "$DIR/jxl.so"; then
      echo "Installed to $DIR" >&2
      exit 0
    fi
    echo "Failed to install to $DIR - are you root?" >&2
    exit 1
  fi
done

echo "Can't find your imlib2 loaders directory." >&2
exit 1
