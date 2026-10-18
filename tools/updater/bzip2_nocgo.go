//go:build !cgo

package main

import "errors"

func libbzip2Available() bool {
	return false
}

func bspatchLibBzip2(oldData []byte, patchData []byte) ([]byte, error) {
	return nil, errors.New("libbzip2 not available")
}
