function downloadDeleteButton(filePath, action) {
  var url = '/' + action + '?file=' + encodeURIComponent(filePath);
  if (action === 'download') {
    window.location.href = url;
  } else if (action === 'delete') {
    if (confirm("Are you sure you want to delete this file?")) {
      fetch(url, { method: 'DELETE' })
        .then(response => response.text())
        .then(text => {
          console.log(text);
          loadLogs(); // Refresh the Logs list after deletion
        })
        .catch(error => console.error('Error deleting file:', error));
    }
  }
}

function rebootESP() {
  if (confirm("Are you sure you want to reboot the ESP32S3?")) {
    fetch('/reboot')
      .then(response => response.text())
      .then(text => console.log('Reboot command sent:', text))
      .catch(error => console.error('Error sending reboot command:', error));
  } else {
    console.log('Reboot canceled.');
  }
}

function scanNetworks() { // Function to scan networks and populate the dropdown
  fetch('/scan-networks')
    .then(response => response.json())
    .then(networks => {
      const networkList = document.getElementById('network-list');
      networkList.innerHTML = '<option value="">Select an SSID</option>'; // Reset dropdown

      networks.forEach(network => {
        const option = document.createElement('option');
        option.textContent = `${network.ssid} (${network.signal_strength} dBm)`;
        option.value = network.ssid; // Set value to SSID
        networkList.appendChild(option);
      });
    })
    .catch(error => {
      console.error('Error fetching network list:', error);
    });
}


function handleSelectionChange() { // Function to handle changes in the dropdown or input field
  const networkList = document.getElementById('network-list');
  const manualSSID = document.getElementById('manual-ssid');

  // If a dropdown option is selected, clear the input field
  if (networkList.value) {
    manualSSID.value = ''; // Clear manual input
  }
}

// Function to handle form submission
function connectToNetwork() {
  const networkList = document.getElementById('network-list');
  const manualSSID = document.getElementById('manual-ssid').value.trim();
  const ssid = networkList.value || manualSSID; // Use selected SSID or manual input
  const password = document.getElementById('password').value;

  if (!ssid) {
    alert('Please enter or select an SSID.');
    return;
  }

  // Send connection request to the server
  fetch('/connect', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
    },
    body: JSON.stringify({ ssid, password }),
  })
    .then(response => response.text())
    .then(result => {
      console.log('Connection result:', result);
      // Handle connection result
    })
    .catch(error => {
      console.error('Error connecting to network:', error);
    });
}

// Initialize dropdown buttons
const dropdownButtons = document.getElementsByClassName("dropdown-btn");
Array.from(dropdownButtons).forEach(button => {
  button.addEventListener("click", function () {
    this.classList.toggle("active"); // Toggle the active class to show/hide the dropdown content
    const dropdownContent = this.nextElementSibling;
    dropdownContent.style.display = dropdownContent.style.display === "block" ? "none" : "block";
  });
});

// Handle page navigation
const sections = document.querySelectorAll('.content-section');

// Function to show the section with the given ID
function showSection(id) {
  sections.forEach(section => {
    const isActive = section.id === id;
    section.classList.toggle('active', isActive);

    if (id === 'update-firmware' && isActive) {
      window.open('/update', '_blank');
    }

    if (id == 'serial-messages' && isActive) {
      window.open('/webserial', '_blank');
    }
  });
}

// Event listeners for navigation links
document.querySelectorAll('.sidenav a').forEach(link => {
  link.addEventListener('click', event => {
    event.preventDefault();
    const targetId = link.getAttribute('href').substring(1);
    showSection(targetId);
  });
});

// Initialize first section
showSection('main');

// Handle Image Stream
document.addEventListener("DOMContentLoaded", () => {
  let streaming = false;  // Flag to track streaming status
  const streamImg = document.getElementById('stream');
  const streamBtn = document.getElementById('stream-btn');
  let streamInterval = null;

  if (streamBtn) {
    // Click event for stream button
    streamBtn.addEventListener('click', () => {
      if (streaming) {
        // Stop streaming
        streamBtn.textContent = 'Start Stream';
        streaming = false;
        fetch('/stopStream');  // Send request to stop streaming

        // Clear interval to stop fetching frames
        clearInterval(streamInterval);
        streamInterval = null;

        // The last image remains displayed
      } else {
        // Start streaming
        streamBtn.textContent = 'Stop Stream';
        streaming = true;
        fetch('/startStream');  // Send request to start streaming

        // Set interval to fetch new frames
        streamInterval = setInterval(() => {
          if (streaming) {
            fetch('/stream')
              .then(response => response.text())
              .then(text => {
                if (text.length > 0) {
                  streamImg.src = 'data:image/jpeg;base64,' + text;
                } else {
                  console.log('No image received.');
                }
              })
              .catch(error => console.error('Error fetching stream:', error));
          }
        }, 50); // Fetch new frames every 50 milliseconds
      }
    });
  } else {
    console.error('Stream button not found.');
  }

  // Handle Lock/Unlock Button
  const lockBtn = document.getElementById('lock-btn');
  let locked = false;

  if (lockBtn) {
    lockBtn.addEventListener('click', () => {
      if (locked) {
        // Unlock the system
        fetch('/lock');
        lockBtn.textContent = 'Unlock';
      } else {
        // Lock the system
        fetch('/unlock');
        lockBtn.textContent = 'Lock';
      }
      locked = !locked;
    });
  } else {
    console.error('Lock button not found.');
  }

  // Fetch and display alerts
  const alertsBox = document.getElementById('alerts-box');

  function loadAlerts() {
    fetch('/alerts')
      .then(response => response.text())
      .then(text => {
        if (text.length > 0) {
          alertsBox.innerHTML = text.replace(/\n/g, '<br>'); // Replace newlines with <br>
        } else {
          alertsBox.innerHTML = '<p>No alerts to show.</p>';
        }
      })
      .catch(error => {
        console.error('Error fetching alerts:', error);
        alertsBox.innerHTML = '<p>Error loading alerts.</p>';
      });
  }

  // Initial load of alerts
  loadAlerts();

  // Refresh alerts every minute
  setInterval(loadAlerts, 60000); // Refresh every 60,000 ms (1 minute)

  // Fetch and display Logs
  const logsList = document.getElementById('logs-list');

  function loadLogs() {
    fetch('/logs')
      .then(response => response.text())
      .then(text => {
        const logs = text.split('\n').filter(file => file.trim() !== ''); // Split the text by newlines
        if (logs.length > 0) {
          logsList.innerHTML = logs;
        } else {
          logsList.innerHTML = '<p>No logs available.</p>';
        }
      })
      .catch(error => {
        console.error('Error fetching logs:', error);
        logsList.innerHTML = '<p>Error loading logs.</p>';
      });
  }

  function playRecording(filename) {
    console.log(`Playing recording: ${filename}`);
    fetch(`/play-recording?file=${filename}`)
      .then(response => response.text())
      .then(data => {
        console.log('Data received:', data);
        const frames = data.split('\n').slice(1); // Discard the first frame (title)
        const totalFrames = frames.length;
        const intervalTime = 30000 / totalFrames; // 30 seconds divided by the number of frames

        console.log(`Total frames: ${totalFrames}`);
        console.log(`Interval time: ${intervalTime} ms`);

        const recordingsImage = document.getElementById('recordings-image');
        let frameIndex = 0;

        function displayNextFrame() {
          if (frameIndex < totalFrames) {
            console.log(`Displaying frame ${frameIndex + 1}/${totalFrames}`);
            recordingsImage.src = `data:image/jpeg;base64,${frames[frameIndex]}`;
            frameIndex++;
            setTimeout(displayNextFrame, intervalTime);
          } else {
            console.log('All frames displayed.');
          }
        }

        displayNextFrame();
      })
      .catch(error => console.error('Error playing recording:', error));
  }
  // Initial load of logs
  loadLogs();

  // Refresh logs every minute
  // setInterval(loadLogs, 60000); // Refresh every 60,000 ms (1 minute)

  function fetchDeviceInfo() {
    fetch('/device-info')
      .then(response => response.text()) // Fetch as text first
      .then(text => {
        console.log('Raw JSON response:', text); // Log the raw JSON response
        return JSON.parse(text); // Parse the JSON
      })
      .then(data => {
        document.getElementById('device-info').innerHTML = `
          <h3>Device Information</h3>
          <p><strong>Device Model:</strong> ${data.deviceModel}</p>
          <p><strong>Chip ID:</strong> ${data.chipID}</p>
          <p><strong>Chip Cores:</strong> ${data.chipCores}</p>
          <p><strong>MAC Address:</strong> ${data.MAC_Address}</p>
          <p><strong>Firmware:</strong> ${data.firmware}</p>
          <p><strong>Uptime:</strong> ${data.uptime} </p>
          <p><strong>GMT Offset:</strong> ${data.gmtoffset} seconds</p>
          <p><strong>Daylight Offset:</strong> ${data.daylightoffset} minutes</p>
          <p><strong>Interval:</strong> ${data.interval} ms</p>
          <button onclick="rebootESP()">Reboot ESP32S3</button>
        `;
        document.getElementById('wifi-status').innerHTML = `
          <p><strong>WiFi:</strong> ${data.wificonnected ? 'Connected' : 'Disconnected'}</p>
          <p><strong>Signal Strength:</strong> ${data.signalstrength} dBm</p>
          <p><strong>SSID:</strong> ${data.ssid}</p>
          <p><strong>WiFi Password:</strong> ${data.wifi_password}</p>
          <p><strong>IP Address:</strong> ${data.IP}</p>
          <p><strong>Hostname:</strong> ${data.hostname}</p>
        `;
      })
      .catch(error => console.error('Error fetching device info:', error));
  }

  function listRecordings() {
    fetch('/list-recordings') // Adjust the URL to your endpoint
      .then(response => response.json())
      .then(data => {
        const recordingsContainer = document.getElementById('recordings-list');
        recordingsContainer.innerHTML = ''; // Clear any existing content

        data.forEach(recording => {
          const recordingElement = document.createElement('div');
          recordingElement.classList.add('recording');

          const nameElement = document.createElement('p');
          nameElement.textContent = `Name: ${recording.name}`;
          recordingElement.appendChild(nameElement);

          const sizeElement = document.createElement('p');
          sizeElement.textContent = `Size: ${recording.size} bytes`;
          recordingElement.appendChild(sizeElement);

          const playButton = document.createElement('button');
          playButton.textContent = 'Play';
          // playButton.onclick = () => playRecording(recording.name);
          playButton.addEventListener('click', () => playRecording(recording.name));
          recordingElement.appendChild(playButton);

          recordingsContainer.appendChild(recordingElement);
        });
      })
      .catch(error => console.error('Error fetching recordings list:', error));
  }

  // Initialize recordings list on page load
  listRecordings();

  // Initial load of device info
  fetchDeviceInfo();
  // Refresh device info every minute (optional)
  // setInterval(fetchDeviceInfo, 60000); // Refresh every 60,000 ms (1 minute)

  // Initialize network scan and form submission
  scanNetworks();

  // Set up event listeners for network connection
  const networkList = document.getElementById('network-list');
  const manualSSID = document.getElementById('manual-ssid');
  const connectBtn = document.getElementById('connect-btn');

  if (networkList) {
    networkList.addEventListener('change', handleSelectionChange);
  } else {
    console.error('Network list not found.');
  }

  if (manualSSID) {
    manualSSID.addEventListener('input', handleSelectionChange);
  } else {
    console.error('Manual SSID input not found.');
  }

  if (connectBtn) {
    connectBtn.addEventListener('click', connectToNetwork);
  } else {
    console.error('Connect button not found.');
  }
});

