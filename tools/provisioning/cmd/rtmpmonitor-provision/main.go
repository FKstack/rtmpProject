package main

import (
	"flag"
	"fmt"
	"os"

	"rtmpmonitor.local/provisioning/internal/provision"
)

func main() {
	if err := run(os.Args[1:]); err != nil {
		fmt.Fprintln(os.Stderr, "provision_error:", err)
		os.Exit(2)
	}
}

func run(args []string) error {
	if len(args) == 0 {
		return fmt.Errorf("expected device, pair, or validate")
	}
	switch args[0] {
	case "device":
		return createDevice(args[1:])
	case "pair":
		return createPair(args[1:])
	case "validate":
		return validate(args[1:])
	default:
		return fmt.Errorf("unknown command")
	}
}

func createDevice(args []string) error {
	if len(args) == 0 || args[0] != "create" {
		return fmt.Errorf("expected device create")
	}
	flags := flag.NewFlagSet("device create", flag.ContinueOnError)
	device := flags.String("device-id", "", "device identity")
	target := flags.String("control-target-id", "", "legacy control target identity")
	out := flags.String("out", "", "artifact output")
	if err := flags.Parse(args[1:]); err != nil {
		return err
	}
	if flags.NArg() != 0 {
		return fmt.Errorf("unexpected positional arguments")
	}
	if *out == "" {
		return fmt.Errorf("--out is required")
	}
	artifact, err := provision.DeviceArtifact(*device, *target)
	if err != nil {
		return err
	}
	if err = provision.WriteAtomic(*out, artifact); err != nil {
		return err
	}
	fmt.Printf("created type=device subject=%s rules=%d\n", provision.Redact(*device), len(artifact.Permissions))
	return nil
}

func createPair(args []string) error {
	if len(args) == 0 || args[0] != "create" {
		return fmt.Errorf("expected pair create")
	}
	flags := flag.NewFlagSet("pair create", flag.ContinueOnError)
	device := flags.String("device-id", "", "device identity")
	target := flags.String("control-target-id", "", "legacy control target identity")
	operator := flags.String("operator-id", "", "operator identity")
	instance := flags.String("client-instance-id", "", "operator client instance")
	scope := flags.String("scope", "", "view or control")
	out := flags.String("out", "", "artifact output")
	if err := flags.Parse(args[1:]); err != nil {
		return err
	}
	if flags.NArg() != 0 {
		return fmt.Errorf("unexpected positional arguments")
	}
	if *out == "" {
		return fmt.Errorf("--out is required")
	}
	artifact, err := provision.PairArtifact(*device, *target, *operator, *instance, *scope)
	if err != nil {
		return err
	}
	if err = provision.WriteAtomic(*out, artifact); err != nil {
		return err
	}
	fmt.Printf("created type=pair subject=%s rules=%d\n", provision.Redact(*device), len(artifact.Permissions))
	return nil
}

func validate(args []string) error {
	flags := flag.NewFlagSet("validate", flag.ContinueOnError)
	input := flags.String("input", "", "artifact input")
	if err := flags.Parse(args); err != nil {
		return err
	}
	if flags.NArg() != 0 {
		return fmt.Errorf("unexpected positional arguments")
	}
	if *input == "" {
		return fmt.Errorf("--input is required")
	}
	data, err := os.ReadFile(*input)
	if err != nil {
		return err
	}
	artifact, err := provision.DecodeStrict(data)
	if err != nil {
		return err
	}
	fmt.Printf("valid type=%s subject=%s rules=%d\n", artifact.ArtifactType, provision.Redact(artifact.Identity.DeviceID), len(artifact.Permissions))
	return nil
}
