from setuptools import setup

package_name = 'ktos_serial_bridge'

setup(
    name=package_name,
    version='1.0.0',
    packages=[package_name],
    data_files=[
        ('share/ament_index/resource_index/packages',
            ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
    ],
    install_requires=['setuptools', 'pyserial'],
    zip_safe=True,
    maintainer='Khalid Hamdou',
    maintainer_email='baamiis7@gmail.com',
    description='ROS 2 serial bridge for KTOS firmware',
    license='GPL-3.0-only',
    entry_points={
        'console_scripts': [
            'serial_bridge_node = ktos_serial_bridge.serial_bridge_node:main',
        ],
    },
)
