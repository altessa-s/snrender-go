// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

package snrender_test

import (
	"context"
	"fmt"
	"github.com/altessa-s/snrender-go"
	"os"
)

func ExampleNotebook_PNG() {
	data, err := os.ReadFile("testdata/supernote-typescript/blank-a6x-3.15.27-two-pages.note")
	if err != nil {
		panic(err)
	}
	notebook, err := snrender.Open(data)
	if err != nil {
		panic(err)
	}
	defer notebook.Close()
	png, err := notebook.PNG(context.Background(), 0, &snrender.Options{Scale: 0.125})
	if err != nil {
		panic(err)
	}
	fmt.Printf("PNG signature: %x\n", png[:8])
	// Output: PNG signature: 89504e470d0a1a0a
}

func ExampleNotebook_Bitmap() {
	data, err := os.ReadFile("testdata/supernote-typescript/blank-a6x-3.15.27-two-pages.note")
	if err != nil {
		panic(err)
	}
	n, err := snrender.Open(data)
	if err != nil {
		panic(err)
	}
	defer n.Close()
	img, err := n.Bitmap(context.Background(), 0, &snrender.Options{Scale: .125})
	if err != nil {
		panic(err)
	}
	fmt.Println(img.Stride == 4*img.Rect.Dx())
	// Output: true
}

func ExampleNotebook_Raster() {
	data, err := os.ReadFile("testdata/supernote-typescript/blank-a6x-3.15.27-two-pages.note")
	if err != nil {
		panic(err)
	}
	n, err := snrender.Open(data)
	if err != nil {
		panic(err)
	}
	defer n.Close()
	raster, err := n.Raster(context.Background(), 0, &snrender.Options{Scale: .125})
	if err != nil {
		panic(err)
	}
	fmt.Println(raster.GrayImage().Bounds() == raster.AlphaImage().Bounds())
	// Output: true
}

func ExamplePageText() {
	pdf, err := os.ReadFile("testdata/pdf-text/r0-offset0.pdf")
	if err != nil {
		panic(err)
	}
	rects := []snrender.Rect{{X: 19, Y: 16, Width: 100, Height: 18}}
	text, err := snrender.PageText(context.Background(), pdf, 0, rects, nil)
	if err != nil {
		panic(err)
	}
	fmt.Println(text)
	// Output: Alpha Beta
}

func ExampleDrawingBitmap() {
	data, err := os.ReadFile("testdata/spd/valid.spd")
	if err != nil {
		panic(err)
	}
	img, err := snrender.DrawingBitmap(context.Background(), data, nil)
	if err != nil {
		panic(err)
	}
	fmt.Println(img.Bounds())
	// Output: (0,0)-(512,256)
}
