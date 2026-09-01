package provision

import (
	"encoding/json"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
)

func TestDeviceArtifact(t *testing.T) {
	a, err := DeviceArtifact("device-1", "target-1")
	if err != nil {
		t.Fatal(err)
	}
	if len(a.Clients) != 2 || len(a.Permissions) != 5 {
		t.Fatalf("unexpected device artifact size")
	}
	data, _ := json.Marshal(a)
	text := string(data)
	for _, forbidden := range []string{`"password"`, `"secret"`, "device/control", "device/status", "#", "+"} {
		if strings.Contains(text, forbidden) {
			t.Fatalf("artifact contains forbidden token %q", forbidden)
		}
	}
}

func TestSharedACLVectors(t *testing.T) {
	_, source, _, _ := runtime.Caller(0)
	root := filepath.Clean(filepath.Join(filepath.Dir(source), "..", "..", "..", ".."))
	data, err := os.ReadFile(filepath.Join(root, "contracts", "signaling", "v1", "acl_vectors.json"))
	if err != nil {
		t.Fatal(err)
	}
	var vectors struct {
		SchemaVersion   string `json:"schemaVersion"`
		OperatorPublish []struct {
			Topic   string `json:"topic"`
			View    bool   `json:"view"`
			Control bool   `json:"control"`
		} `json:"operatorPublish"`
	}
	if err := json.Unmarshal(data, &vectors); err != nil {
		t.Fatal(err)
	}
	if vectors.SchemaVersion != "signaling-acl-vectors/v1" {
		t.Fatal("unknown shared vector version")
	}
	for _, scope := range []string{"view", "control"} {
		artifact, err := PairArtifact("device-1", "target-1", "operator-1", "desktop-1", scope)
		if err != nil {
			t.Fatal(err)
		}
		published := map[string]bool{}
		for _, permission := range artifact.Permissions {
			if permission.PrincipalRef == "operator-signal" || permission.PrincipalRef == "operator-control" {
				if permission.Direction == "publish" {
					published[permission.Topic] = true
				}
			}
		}
		for _, vector := range vectors.OperatorPublish {
			expected := vector.View
			if scope == "control" {
				expected = vector.Control
			}
			if published[vector.Topic] != expected {
				t.Fatalf("shared ACL mismatch for %s", vector.Topic)
			}
		}
	}
}

func TestPairScopes(t *testing.T) {
	view, err := PairArtifact("device-1", "target-1", "operator-1", "desktop-1", "view")
	if err != nil {
		t.Fatal(err)
	}
	control, err := PairArtifact("device-1", "target-1", "operator-1", "desktop-1", "control")
	if err != nil {
		t.Fatal(err)
	}
	if len(view.Clients) != 3 || len(control.Clients) != 4 {
		t.Fatal("control client scope mismatch")
	}
	for _, permission := range view.Permissions {
		if strings.Contains(permission.Topic, "/control/") {
			t.Fatal("view artifact grants control")
		}
	}
	if len(control.Permissions) != len(view.Permissions)+6 {
		t.Fatal("control permission delta mismatch")
	}

	control.Permissions[0].Topic += "/#"
	if Validate(control) == nil {
		t.Fatal("wildcard permission accepted")
	}
}

func TestStrictDecodeAndAtomicWrite(t *testing.T) {
	a, err := PairArtifact("device-1", "target-1", "operator-1", "desktop-1", "control")
	if err != nil {
		t.Fatal(err)
	}
	directory := t.TempDir()
	path := filepath.Join(directory, "pair.json")
	if err := WriteAtomic(path, a); err != nil {
		t.Fatal(err)
	}
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := DecodeStrict(data); err != nil {
		t.Fatal(err)
	}
	mutated := strings.Replace(string(data), `"schemaVersion":`, `"unknown":1,"schemaVersion":`, 1)
	if _, err := DecodeStrict([]byte(mutated)); err == nil {
		t.Fatal("unknown field accepted")
	}
	if info, err := os.Stat(path); runtime.GOOS != "windows" && (err != nil || info.Mode().Perm()&0o077 != 0) {
		t.Fatalf("artifact permission is not private: %v %v", info, err)
	}
}

func FuzzDecodeStrict(f *testing.F) {
	a, _ := PairArtifact("device-1", "target-1", "operator-1", "desktop-1", "view")
	seed, _ := json.Marshal(a)
	f.Add(seed)
	f.Add([]byte(`{"schemaVersion":1}`))
	f.Add([]byte{0xff})
	f.Fuzz(func(t *testing.T, data []byte) { _, _ = DecodeStrict(data) })
}
