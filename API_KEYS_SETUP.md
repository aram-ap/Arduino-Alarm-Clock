# API Keys Setup

## Security Notice
API keys are now stored in a separate `env.h` file to keep them out of version control.

## Setup Instructions

1. **Copy the template file**:
   ```bash
   cp env.h.template src/env.h
   ```

2. **Edit src/env.h** and replace the placeholder values with your actual API keys:
   ```cpp
   #define IP2LOCATION_KEY "your_actual_ip2location_key"
   #define OPENWEATHER_API_KEY "your_actual_openweather_key"
   ```

3. **Get your API keys**:
   - **IP2Location**: Sign up at https://www.ip2location.com/web-service
   - **OpenWeather**: Sign up at https://openweathermap.org/api

## Security Features

- ✅ `src/env.h` is in `.gitignore` - won't be committed to version control
- ✅ Template file shows structure without revealing keys
- ✅ API keys are centralized in one file
- ✅ Easy to update keys without touching main code

## Current API Keys Used

- **IP2Location**: Used for automatic location detection and timezone setting
- **OpenWeather**: Used for weather information display

## Troubleshooting

If you get compilation errors:
1. Make sure `src/env.h` exists in the src directory
2. Verify your API keys are correct
3. Check that the keys are properly quoted in the file 