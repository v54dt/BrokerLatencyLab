import yaml
import sys
import os


def load_config():
    config_path = os.path.join(os.path.dirname(
        os.path.dirname(__file__)), 'config.toml')
    try:
        import tomllib
    except ImportError:
        import tomli as tomllib
    with open(config_path, 'rb') as f:
        return tomllib.load(f)


def generate_docker_compose(config):
    compose = {
        'services': {},
        'networks': {
            'streambeaver': {
                'driver': 'bridge'
            }
        },
        'volumes': {
            'logs': None,
            'data': None
        }
    }

    for broker_name, broker_config in config['brokers'].items():
        if not broker_config.get('enabled', True):
            continue

        replicas = broker_config.get('replicas', 1)

        for i in range(replicas):
            service_name = f"{broker_name}-{i+1}" if replicas > 1 else broker_name

            service_def = {
                'build': {
                    'context': f'./brokers/{broker_name}',
                    'dockerfile': 'Dockerfile'
                },
                'container_name': service_name,
                'environment': [f"TZ={config['global']['timezone']}"],
                'network_mode': 'host',
                'restart': 'unless-stopped',
                'stdin_open': True,
                'tty': True
            }

            # Add resource limits
            service_def['deploy'] = {
                'resources': {
                    'limits': {
                        'cpus': broker_config.get('cpu_limit', '0.5'),
                        'memory': broker_config.get('memory_limit', '256M')
                    }
                }
            }
            if 'cpu_reservation' in broker_config:
                service_def['deploy']['resources']['reservations'] = {
                    'cpus': broker_config['cpu_reservation'],
                    'memory': broker_config.get('memory_reservation', '128M')
                }

            compose['services'][service_name] = service_def

    return compose


def main():
    config = load_config()
    compose = generate_docker_compose(config)

    print(yaml.dump(compose, default_flow_style=False, sort_keys=False))


if __name__ == '__main__':
    main()
