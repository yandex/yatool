package greeting

import "testing"

func TestSayHello(t *testing.T) {
	var tests = []struct {
		value, result string
	}{
		{value: "World", result: "Hello, World!"},
		{value: "GO", result: "Hello, GO!"},
		{value: "", result: "Hello, !"},
	}

	for _, test := range tests {
		if result := SayHello(test.value); result != test.result {
			t.Errorf("SayHello(%v) = %v, expected %v", test.value, result, test.result)
		}
	}
}
