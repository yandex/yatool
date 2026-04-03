package main

import (
	"flag"
	"log"
	"os"
	"os/exec"
	"syscall"
)

func main() {
	flag.Parse()

	if len(flag.Args()) == 0 {
		log.Fatalf("No command to execute...\n")
	}

	cmd := exec.Command(flag.Args()[0], flag.Args()[1:]...)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		if exiterr, ok := err.(*exec.ExitError); ok {
			if status, ok := exiterr.Sys().(syscall.WaitStatus); ok {
				os.Exit(status.ExitStatus())
			}
		} else {
			log.Fatalf("cmd.Run: %v\n", err)
		}
	}
}
