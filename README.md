
# cOrange

## Description

C implementation of the OrangeHRM API Client

## Features

* **Feature 1**: Token generation.
* **Feature 2**: Rest method implemented.
* **Feature 3**: Demo GUI Application for Attendance Punch in punch out   

## Requirements

Before running the project, make sure you have the following installed:

* C compiler (e.g., GCC)
* `libcurl` library
* `json-c` library

### Installing Dependencies (for Linux)

Use the following commands to install the necessary dependencies (for example, on Ubuntu):

```bash
sudo apt update
sudo apt install libcurl4-openssl-dev libjson-c-dev libgtk-3-dev
```

## Getting Started

To get started with this project, follow the steps below:

### Cloning the Repository

Clone the repository to your local machine:

```bash
git clone https://github.com/your-username/your-repository-name.git
cd your-repository-name
```

### Building the Project

1. **Create a build directory** (recommended):

   ```bash
   mkdir build
   cd build
   ```

2. **Run CMake to configure the project**:

   ```bash
   cmake ..
   ```

3. **Build the project**:

   ```bash
   make
   ```

4. **Run the application**:

   After building the project, you can run the compiled executable:

   ```bash
   ./your_executable_name
   ```

## Usage

Once the application is running, you can interact with it by providing the necessary input, such as [example of inputs]. The program will return [expected output].

## Configuration

The application uses a `config.json` file for configuration. Below is an example of the required structure for the `config.json` file:

```json
{
    "base_url": "https://api.example.com",
    "access_token": "your_access_token",
    "client_id": "your_client_id",
    "client_secret": "your_client_secret",
    "username": "your_employee number"
}
```

Make sure to replace the values with your actual configuration details.

## Contributing

If you'd like to contribute to this project, follow these steps:

1. Fork the repository.
2. Create a new branch (`git checkout -b feature-branch`).
3. Make your changes and commit them (`git commit -am 'Add new feature'`).
4. Push to the branch (`git push origin feature-branch`).
5. Open a pull request.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
