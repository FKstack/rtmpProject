package provision

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
)

const (
	SchemaVersion      = "provisioning-artifact/v1"
	SecretProvisioning = "deferred"
	topicRoot          = "rtmp-monitor/v1"
)

var opaqueID = regexp.MustCompile(`^[A-Za-z0-9._-]{1,128}$`)

type Subject struct {
	Kind string `json:"kind"`
}

type Identity struct {
	DeviceID       string `json:"deviceId"`
	ControlTarget  string `json:"mqttControlTargetId"`
	OperatorID     string `json:"userId,omitempty"`
	ClientInstance string `json:"clientInstanceId,omitempty"`
}

type Client struct {
	PrincipalRef      string `json:"principalRef"`
	Plane             string `json:"plane"`
	ClientID          string `json:"clientId"`
	Username          string `json:"username"`
	CredentialRef     string `json:"credentialRef"`
	CredentialVersion int    `json:"credentialVersion"`
	Status            string `json:"status"`
}

type Permission struct {
	PrincipalRef string `json:"principalRef"`
	Direction    string `json:"direction"`
	Topic        string `json:"topic"`
	QoS          int    `json:"qos"`
	RetainPolicy string `json:"retainPolicy"`
}

type Lifecycle struct {
	RotationImplementationStatus string `json:"rotationImplementationStatus"`
	RevokeImplementationStatus   string `json:"revokeImplementationStatus"`
}

type Artifact struct {
	SchemaVersion      string       `json:"schemaVersion"`
	ArtifactType       string       `json:"artifactType"`
	Scope              string       `json:"scope,omitempty"`
	Subject            Subject      `json:"subject"`
	Identity           Identity     `json:"identity"`
	Clients            []Client     `json:"clients"`
	Permissions        []Permission `json:"permissions"`
	Lifecycle          Lifecycle    `json:"lifecycle"`
	SecretProvisioning string       `json:"secretProvisioning"`
}

func DeviceArtifact(deviceID, controlTarget string) (Artifact, error) {
	if err := validateIDs(deviceID, controlTarget); err != nil {
		return Artifact{}, err
	}
	a := base("device", "", deviceID, controlTarget, "", "")
	a.Clients = deviceClients(deviceID)
	a.Permissions = baseDevicePermissions(deviceID)
	return a, Validate(a)
}

func PairArtifact(deviceID, controlTarget, operatorID, instanceID, scope string) (Artifact, error) {
	if err := validateIDs(deviceID, controlTarget, operatorID, instanceID); err != nil {
		return Artifact{}, err
	}
	if scope != "view" && scope != "control" {
		return Artifact{}, errors.New("scope must be view or control")
	}
	a := base("pair", scope, deviceID, controlTarget, operatorID, instanceID)
	a.Clients = append(deviceClients(deviceID), client("operator-signal", "signal", operatorClientID(operatorID, instanceID, "signal")))
	if scope == "control" {
		a.Clients = append(a.Clients, client("operator-control", "control", operatorClientID(operatorID, instanceID, "control")))
	}
	a.Permissions = pairPermissions(a.Identity, scope)
	return a, Validate(a)
}

func base(kind, scope, deviceID, target, operatorID, instanceID string) Artifact {
	return Artifact{SchemaVersion: SchemaVersion, ArtifactType: kind, Scope: scope,
		Subject: Subject{Kind: kind}, Identity: Identity{DeviceID: deviceID, ControlTarget: target, OperatorID: operatorID, ClientInstance: instanceID},
		Lifecycle: Lifecycle{RotationImplementationStatus: "deferred", RevokeImplementationStatus: "deferred"}, SecretProvisioning: SecretProvisioning}
}

func client(ref, plane, id string) Client {
	return Client{PrincipalRef: ref, Plane: plane, ClientID: id, Username: id,
		CredentialRef: "credential-ref:v1/" + ref, CredentialVersion: 1, Status: "pending"}
}

func deviceClients(deviceID string) []Client {
	return []Client{client("device-signal", "signal", "device-"+deviceID+"-signal"), client("device-control", "control", "device-"+deviceID+"-control")}
}

func operatorClientID(operatorID, instanceID, plane string) string {
	return "operator-" + operatorID + "-" + instanceID + "-" + plane
}

func perm(principal, direction, topic string) Permission {
	return Permission{PrincipalRef: principal, Direction: direction, Topic: topic, QoS: 1, RetainPolicy: "forbidden"}
}

func baseDevicePermissions(deviceID string) []Permission {
	base := topicRoot
	return []Permission{
		perm("device-signal", "publish", base+"/presence/device/"+deviceID),
		perm("device-signal", "publish", base+"/capabilities/device/"+deviceID),
		perm("device-signal", "publish", base+"/busy/device/"+deviceID),
		perm("device-signal", "publish", base+"/telemetry/device/"+deviceID+"/heartbeat"),
		perm("device-signal", "publish", base+"/telemetry/device/"+deviceID+"/snapshot"),
	}
}

func pairPermissions(id Identity, scope string) []Permission {
	base := topicRoot
	toDevice := base + "/signaling/to/device/" + id.DeviceID + "/from/operator/" + id.OperatorID + "/" + id.ClientInstance
	toOperator := base + "/signaling/to/operator/" + id.OperatorID + "/" + id.ClientInstance + "/from/device/" + id.DeviceID
	rules := baseDevicePermissions(id.DeviceID)
	for _, topic := range []string{base + "/presence/device/" + id.DeviceID, base + "/capabilities/device/" + id.DeviceID,
		base + "/busy/device/" + id.DeviceID, base + "/telemetry/device/" + id.DeviceID + "/heartbeat", base + "/telemetry/device/" + id.DeviceID + "/snapshot"} {
		rules = append(rules, perm("operator-signal", "subscribe", topic))
	}
	rules = append(rules, perm("operator-signal", "publish", toDevice), perm("device-signal", "subscribe", toDevice),
		perm("device-signal", "publish", toOperator), perm("operator-signal", "subscribe", toOperator))
	if scope == "control" {
		command := base + "/control/to/device/" + id.ControlTarget + "/from/operator/" + id.OperatorID + "/" + id.ClientInstance + "/command"
		receipt := base + "/control/to/operator/" + id.OperatorID + "/" + id.ClientInstance + "/from/device/" + id.ControlTarget + "/receipt"
		safety := base + "/control/to/operator/" + id.OperatorID + "/" + id.ClientInstance + "/from/device/" + id.ControlTarget + "/safety"
		rules = append(rules, perm("operator-control", "publish", command), perm("device-control", "subscribe", command),
			perm("device-control", "publish", receipt), perm("operator-control", "subscribe", receipt),
			perm("device-control", "publish", safety), perm("operator-control", "subscribe", safety))
	}
	return rules
}

func DecodeStrict(data []byte) (Artifact, error) {
	decoder := json.NewDecoder(bytes.NewReader(data))
	decoder.DisallowUnknownFields()
	var artifact Artifact
	if err := decoder.Decode(&artifact); err != nil {
		return Artifact{}, fmt.Errorf("invalid artifact: %w", err)
	}
	if err := ensureEOF(decoder); err != nil {
		return Artifact{}, err
	}
	if err := Validate(artifact); err != nil {
		return Artifact{}, err
	}
	return artifact, nil
}

func ensureEOF(decoder *json.Decoder) error {
	var extra any
	if err := decoder.Decode(&extra); err != io.EOF {
		return errors.New("artifact contains trailing JSON")
	}
	return nil
}

func Validate(a Artifact) error {
	if a.SchemaVersion != SchemaVersion || a.SecretProvisioning != SecretProvisioning {
		return errors.New("unsupported or secret-bearing artifact")
	}
	if a.Lifecycle.RotationImplementationStatus != "deferred" || a.Lifecycle.RevokeImplementationStatus != "deferred" {
		return errors.New("lifecycle execution must remain deferred")
	}
	if a.Subject.Kind != a.ArtifactType || (a.ArtifactType != "device" && a.ArtifactType != "pair") {
		return errors.New("invalid artifact subject")
	}
	if err := validateIDs(a.Identity.DeviceID, a.Identity.ControlTarget); err != nil {
		return err
	}
	var expected Artifact
	var err error
	if a.ArtifactType == "device" {
		if a.Scope != "" || a.Identity.OperatorID != "" || a.Identity.ClientInstance != "" {
			return errors.New("device artifact contains pair fields")
		}
		expected, err = DeviceArtifactUnchecked(a.Identity.DeviceID, a.Identity.ControlTarget)
	} else {
		if err = validateIDs(a.Identity.OperatorID, a.Identity.ClientInstance); err != nil {
			return err
		}
		if a.Scope != "view" && a.Scope != "control" {
			return errors.New("invalid pair scope")
		}
		expected = base("pair", a.Scope, a.Identity.DeviceID, a.Identity.ControlTarget, a.Identity.OperatorID, a.Identity.ClientInstance)
		expected.Clients = append(deviceClients(a.Identity.DeviceID), client("operator-signal", "signal", operatorClientID(a.Identity.OperatorID, a.Identity.ClientInstance, "signal")))
		if a.Scope == "control" {
			expected.Clients = append(expected.Clients, client("operator-control", "control", operatorClientID(a.Identity.OperatorID, a.Identity.ClientInstance, "control")))
		}
		expected.Permissions = pairPermissions(expected.Identity, a.Scope)
	}
	if err != nil {
		return err
	}
	if !sameClients(a.Clients, expected.Clients) {
		return errors.New("client set does not match scope")
	}
	if !samePermissions(a.Permissions, expected.Permissions) {
		return errors.New("permission set is not exact")
	}
	for _, p := range a.Permissions {
		if strings.ContainsAny(p.Topic, "#+") || !strings.HasPrefix(p.Topic, topicRoot+"/") || p.RetainPolicy != "forbidden" || p.QoS != 1 {
			return errors.New("unsafe permission")
		}
	}
	return nil
}

func DeviceArtifactUnchecked(deviceID, controlTarget string) (Artifact, error) {
	a := base("device", "", deviceID, controlTarget, "", "")
	a.Clients = deviceClients(deviceID)
	a.Permissions = baseDevicePermissions(deviceID)
	return a, nil
}

func sameClients(left, right []Client) bool {
	if len(left) != len(right) {
		return false
	}
	key := func(c Client) string { b, _ := json.Marshal(c); return string(b) }
	a, b := make([]string, len(left)), make([]string, len(right))
	for i, v := range left {
		a[i] = key(v)
	}
	for i, v := range right {
		b[i] = key(v)
	}
	sort.Strings(a)
	sort.Strings(b)
	return strings.Join(a, "\n") == strings.Join(b, "\n")
}

func samePermissions(left, right []Permission) bool {
	if len(left) != len(right) {
		return false
	}
	key := func(p Permission) string { b, _ := json.Marshal(p); return string(b) }
	a, b := make([]string, len(left)), make([]string, len(right))
	for i, v := range left {
		a[i] = key(v)
	}
	for i, v := range right {
		b[i] = key(v)
	}
	sort.Strings(a)
	sort.Strings(b)
	return strings.Join(a, "\n") == strings.Join(b, "\n")
}

func validateIDs(values ...string) error {
	for _, value := range values {
		if !opaqueID.MatchString(value) {
			return errors.New("invalid opaque identity")
		}
	}
	return nil
}

func WriteAtomic(path string, artifact Artifact) error {
	if err := Validate(artifact); err != nil {
		return err
	}
	directory := filepath.Dir(path)
	file, err := os.CreateTemp(directory, ".rtmpmonitor-provision-*.tmp")
	if err != nil {
		return err
	}
	temporary := file.Name()
	committed := false
	defer func() {
		if !committed {
			_ = os.Remove(temporary)
		}
	}()
	if err = file.Chmod(0o600); err == nil {
		encoder := json.NewEncoder(file)
		encoder.SetIndent("", "  ")
		err = encoder.Encode(artifact)
	}
	if closeErr := file.Close(); err == nil {
		err = closeErr
	}
	if err != nil {
		return err
	}
	if err = os.Rename(temporary, path); err != nil {
		return err
	}
	committed = true
	return nil
}

func Redact(id string) string {
	if len(id) <= 4 {
		return fmt.Sprintf("***[%d]", len(id))
	}
	return fmt.Sprintf("%s…[%d]", id[:4], len(id))
}
