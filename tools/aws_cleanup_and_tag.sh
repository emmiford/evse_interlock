#!/usr/bin/env bash
set -euo pipefail

REGION="${AWS_REGION:-us-east-1}"

# Default all output to bash_output.txt unless overridden.
OUTPUT_FILE="${OUTPUT_FILE:-bash_output.txt}"
exec >"$OUTPUT_FILE" 2>&1

# Enable command tracing when DEBUG=1.
if [[ "${DEBUG:-0}" == "1" ]]; then
  set -x
fi

# DRY_RUN=1 prints delete actions without executing them.
DRY_RUN="${DRY_RUN:-0}"
run_or_echo() {
  if [[ "$DRY_RUN" == "1" ]]; then
    echo "DRY_RUN: $*"
  else
    "$@"
  fi
}

# ---- KEEP (from repo) ----
KEEP_WIRELESS_DEVICE_ID="6b3b5b8e-eaa9-4ab6-8720-030b1c1d2f61"
KEEP_DEVICE_PROFILE_ID="d01c298b-0a71-4ec6-9cb6-33d8ae2286b9"
declare -a KEEP_DESTINATION_NAMES=("SensorAppDestination" "RAK4630_Sidewalk_Dest")
# NOTE: IoT Core certificate IDs are 64-hex. We resolve cert ARNs from the
# associated Thing instead of hard-coding a cert ID.

# ---- Naming + tags ----
SITE_ID_FULL="gbvuxz4gl4ndf2y4lxe6n5im4yratiib2eqfcl7v53paiaayqntg2ddedziv4kdxsr5v3vf4gdjq"
SITE_ID_SHORT="gbvuxz4g"
SHORT_SERIAL="89D54998"
NEW_WIRELESS_NAME="sw-${SITE_ID_SHORT}-${SHORT_SERIAL}"

DEVICE_TYPE="EVSE"
CUSTOMER_ID="default"
INSTALL_ZIP="80211"
INSTALL_ADDR_HASH="${SITE_ID_FULL}"
FIRMWARE_REV="v1.1.0-add-on"
HARDWARE_REV="RAK4630"

# ---- Resolve wireless device ARN and associated Thing ----
KEEP_WIRELESS_ARN="$(aws --region "$REGION" iotwireless get-wireless-device \
  --identifier "$KEEP_WIRELESS_DEVICE_ID" \
  --identifier-type WirelessDeviceId \
  --query 'Arn' --output text)"
KEEP_THING_NAME="$(aws --region "$REGION" iotwireless get-wireless-device \
  --identifier "$KEEP_WIRELESS_DEVICE_ID" \
  --identifier-type WirelessDeviceId \
  --query 'ThingName' --output text)"

declare -a KEEP_THING_NAMES=()
if [[ "$KEEP_THING_NAME" != "None" && "$KEEP_THING_NAME" != "null" && -n "$KEEP_THING_NAME" ]]; then
  KEEP_THING_NAMES+=("$KEEP_THING_NAME")
else
  echo "WARN: Wireless device has no ThingName; cert/policy cleanup will be skipped."
fi

# ---- Resolve keep cert ARNs from Thing principals (if any) ----
declare -a KEEP_CERT_ARNS=()
if [[ ${#KEEP_THING_NAMES[@]} -gt 0 ]]; then
  for t in "${KEEP_THING_NAMES[@]}"; do
    for pr in $(aws --region "$REGION" iot list-thing-principals \
      --thing-name "$t" --query 'principals[]' --output text); do
      KEEP_CERT_ARNS+=("$pr")
    done
  done
fi

# ---- Policies attached to keep certs ----
declare -a KEEP_POLICY_NAMES=()
if [[ ${#KEEP_CERT_ARNS[@]} -gt 0 ]]; then
  for cert_arn in "${KEEP_CERT_ARNS[@]}"; do
    for p in $(aws --region "$REGION" iot list-attached-policies --target "$cert_arn" \
      --query 'policies[].policyName' --output text); do
      KEEP_POLICY_NAMES+=("$p")
    done
  done
fi

# ---- Rename + Tag the kept wireless device ----
run_or_echo aws --region "$REGION" iotwireless update-wireless-device \
  --id "$KEEP_WIRELESS_DEVICE_ID" \
  --name "$NEW_WIRELESS_NAME"

run_or_echo aws --region "$REGION" iotwireless tag-resource \
  --resource-arn "$KEEP_WIRELESS_ARN" \
  --tags \
    "Key=customerId,Value=$CUSTOMER_ID" \
    "Key=siteId,Value=$SITE_ID_SHORT" \
    "Key=installZip,Value=$INSTALL_ZIP" \
    "Key=installAddrHash,Value=$INSTALL_ADDR_HASH" \
    "Key=hardwareRev,Value=$HARDWARE_REV" \
    "Key=firmwareRev,Value=$FIRMWARE_REV" \
    "Key=deviceType,Value=$DEVICE_TYPE"

# Optionally tag the IoT Thing (Thing names are immutable)
# Thing attributes are limited to 3 without a Thing Type. We skip updates here
# to avoid InvalidRequestException; use Wireless Device tags instead.

# ---- Cleanup: wireless devices (delete all except keep) ----
for id in $(aws --region "$REGION" iotwireless list-wireless-devices --query 'WirelessDeviceList[].Id' --output text); do
  if [[ "$id" != "$KEEP_WIRELESS_DEVICE_ID" ]]; then
    run_or_echo aws --region "$REGION" iotwireless delete-wireless-device --id "$id"
  fi
done

# ---- Cleanup: destinations (keep explicit list) ----
for d in $(aws --region "$REGION" iotwireless list-destinations --query 'DestinationList[].Name' --output text); do
  keep=false
  for k in "${KEEP_DESTINATION_NAMES[@]}"; do
    [[ "$d" == "$k" ]] && keep=true
  done
  if [[ "$keep" == false ]]; then
    run_or_echo aws --region "$REGION" iotwireless delete-destination --name "$d"
  fi
done

# ---- Cleanup: device profiles (keep explicit list) ----
for p in $(aws --region "$REGION" iotwireless list-device-profiles --query 'DeviceProfileList[].Id' --output text); do
  if [[ "$p" != "$KEEP_DEVICE_PROFILE_ID" ]]; then
    run_or_echo aws --region "$REGION" iotwireless delete-device-profile --id "$p"
  fi
done

# ---- Cleanup: IoT Things (delete all except keep) ----
for t in $(aws --region "$REGION" iot list-things --query 'things[].thingName' --output text); do
  keep=false
  for k in "${KEEP_THING_NAMES[@]}"; do
    [[ "$t" == "$k" ]] && keep=true
  done
  if [[ "$keep" == false ]]; then
    for pr in $(aws --region "$REGION" iot list-thing-principals --thing-name "$t" --query 'principals[]' --output text); do
      run_or_echo aws --region "$REGION" iot detach-thing-principal --thing-name "$t" --principal "$pr"
    done
    run_or_echo aws --region "$REGION" iot delete-thing --thing-name "$t"
  fi
done

# ---- Cleanup: certs (detach policies, deactivate, delete all except keep) ----
if [[ ${#KEEP_CERT_ARNS[@]} -eq 0 ]]; then
  echo "WARN: No keep cert ARNs found; skipping cert/policy cleanup for safety."
else
  for c in $(aws --region "$REGION" iot list-certificates --query 'certificates[].certificateArn' --output text); do
    keep=false
    for k in "${KEEP_CERT_ARNS[@]}"; do
      [[ "$c" == "$k" ]] && keep=true
    done
    if [[ "$keep" == false ]]; then
      for p in $(aws --region "$REGION" iot list-attached-policies --target "$c" --query 'policies[].policyName' --output text); do
        run_or_echo aws --region "$REGION" iot detach-policy --policy-name "$p" --target "$c"
      done
      cert_id="${c##*/}"
      run_or_echo aws --region "$REGION" iot update-certificate --certificate-id "$cert_id" --new-status INACTIVE
      run_or_echo aws --region "$REGION" iot delete-certificate --certificate-id "$cert_id"
    fi
  done
fi

# ---- Cleanup: policies (delete unused except keep policies) ----
if [[ ${#KEEP_CERT_ARNS[@]} -eq 0 ]]; then
  echo "WARN: No keep cert ARNs found; skipping policy cleanup for safety."
else
  for p in $(aws --region "$REGION" iot list-policies --query 'policies[].policyName' --output text); do
    keep=false
    for k in "${KEEP_POLICY_NAMES[@]}"; do
      [[ "$p" == "$k" ]] && keep=true
    done
    if [[ "$keep" == false ]]; then
      targets="$(aws --region "$REGION" iot list-targets-for-policy --policy-name "$p" --query 'targets[]' --output text)"
      if [[ -z "$targets" ]]; then
        run_or_echo aws --region "$REGION" iot delete-policy --policy-name "$p"
      fi
    fi
  done
fi
