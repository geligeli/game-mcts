
Objective: Write a single-file, header-only C++ library named SimplePlot that generates static HTML files for data visualization. The library must use Plotly.js via a CDN for rendering. The data must be embedded directly into the HTML file as a JSON object (no external data files).

✅ COMPLETED - Extended with HTTP serving capability via separate SimpleHTTP library.

Core Constraints:

Dependency Free: Use only the C++ Standard Library (<vector>, <string>, <fstream>, <iostream>, <sstream>, etc.). Do not require external JSON libraries; implement a minimal helper to serialize std::vector to a JSON array string. Use c++23 (e.g. ranges etc..)

Simplicity: The API must be intuitive and minimal.

API Requirements: The library should revolve around a single class (e.g., class Figure or class Plot). It needs to support:

Data Types: Accept std::vector<double> (or templated numerical types).

plot(y): Plots a line graph where X is the index (0, 1, 2...).

plot(x, y): Plots a line graph of X vs Y.

scatter(x, y): Plots a scatter graph (markers only, no lines).

Multiple Traces: Calling plot or scatter multiple times on the same object should add multiple traces to the same chart.

Axes Configuration:

set_log_x(bool): Toggle logarithmic scale for X.

set_log_y(bool): Toggle logarithmic scale for Y.

set_title(string): Set the chart title.

Output: 
- save(filename): Generates the HTML file.
- serve_once(port=8080): Serves the plot via HTTP once and stops.
- serve(port=8080): Serves the plot via HTTP continuously until Ctrl+C.

Implementation Strategy:

Internal Storage: Store traces in a struct/class that holds the x vector, y vector, and a mode string (e.g., "lines", "markers").

HTML Generation:

Write a standard HTML5 boilerplate.

Include the Plotly CDN script: <script src="https://cdn.plot.ly/plotly-latest.min.js"></script>.

Inside a <script> tag, construct the Javascript necessary to render the plot.

Convert the C++ vectors into JavaScript arrays (JSON format) using string stream manipulation.

Plotly Config:

Pass the layout object to Plotly.newPlot, handling the logic for titles and log axes (type: 'log' vs type: 'linear').

HTTP Server (SimpleHTTP):

A separate minimal HTTP server library in http_server.h/.cpp
Uses only POSIX sockets (no external dependencies)
Supports serving HTML content on a configurable port
Two modes: serve_once() and serve() for continuous serving

Example Usage Code (for you to verify against):

C++

int main() {
    visualisations::Figure fig;

    std::vector<double> x = {1, 2, 3, 4, 5};
    std::vector<double> y = {10, 100, 1000, 10000, 100000};

    // Add a line plot
    fig.plot(x, y);

    // Add a scatter plot on top
    fig.scatter(x, y);

    // Configure axes
    fig.set_log_y(true);
    fig.set_title("Logarithmic Growth");

    // Option 1: Save to file
    fig.save("plot.html");
    
    // Option 2: Serve via HTTP (once)
    fig.serve_once(8080);
    
    // Option 3: Serve via HTTP (continuously)
    // fig.serve(8080);
    
    return 0;
}

Output: Provide the complete C++ code in

cpp/visualisations/visualisations.h

and things that are not templated (string constants etc... ) go into 

cpp/visualisations/visualisations.cpp

HTTP server implementation goes into:

cpp/visualisations/http_server.h
cpp/visualisations/http_server.cpp